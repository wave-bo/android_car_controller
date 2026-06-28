/*

       《智能小车状态与蓝牙操作手册》


==============================================================================
【系统工作逻辑与状态】

默认状态 (停车待机)：上电后小车保持静止，等待指令。

避障最高优先级 (全天候守护)：无论小车处于何种模式(循迹/遥控)，只要前置探头
发现障碍，立刻强行接管底盘，执行退让或转弯避险。危险解除后，自动恢复原有模式。

蓝牙遥控模式：接收到手机方向指令，执行点动行驶。

自动循迹模式：沿黑线自动行驶，直到接收到停止指令或偏离轨道。

【手机蓝牙 APP 指令集】(需配置按键发送对应英文字符)
-> 基础控制：
[ F ] : 前进 (Forward)
[ B ] : 后退 (Backward)
[ L ] : 左转 (Left)
[ R ] : 右转 (Right)
[ S ] : 紧急停止 (Stop) - 并且退出自动循迹模式

-> 模式切换：
[ A ] : 开启自动循迹模式 (Auto)

-> 独家功能：机械跑偏校准 (Trim)
如果小车直行时偏向一侧，可发送以下指令进行软件差速微调：
[ + ] : 增加左轮让步时间 (左轮减速，车身偏左)
[ - ] : 减少左轮让步时间 (左轮加速，车身偏右)
[ 0 ] : 恢复默认 0 差速

*/

#include <reg52.h>

// ===================== 电机控制引脚 =====================
sbit RIGHT_MOTOR_A = P1 ^ 0;  // 右电机控制1：B-1A
sbit RIGHT_MOTOR_B = P1 ^ 1;  // 右电机控制2：B-1B
sbit LEFT_MOTOR_A = P1 ^ 2;  // 左电机控制1：第二电机控制线1
sbit LEFT_MOTOR_B = P1 ^ 3;  // 左电机控制2：第二电机控制线2

// ===================== 红外探头输入 =====================
// 1. 向下的循迹探头 (压黑线输出1，白地输出0)
sbit RIGHT_IR = P1 ^ 4;       // 右边循迹红外探头
sbit LEFT_IR = P1 ^ 5;       // 左边循迹红外探头

// 2. 向前的避障探头 (遇障碍输出0，安全输出1)
sbit LEFT_FRONT_IR = P1 ^ 6;  // 左前避障探头
sbit RIGHT_FRONT_IR = P1 ^ 7; // 右前避障探头

sbit KEY_S4 = P3 ^ 2;         // S4 物理按键：按一下启动/停止小车循迹
sbit BUZZER = P3 ^ 6;         // 板载蜂鸣器 BUZ1

// ===================== 常量与参数配置 =====================
#define IR_ON_LINE_LEVEL  0   // 如果你的循迹探头压黑线是高电平，请把这里改成 1
#define KEY_PRESSED_LEVEL 0
#define KEY_DEBOUNCE_MS   20
#define STRAIGHT_RUN_MS   10  // 直走通电运行时间 (用于软件PWM调速)
#define STRAIGHT_PAUSE_MS 6   // 直走断电滑行时间
#define TURN_RUN_MS       10  // 转向通电运行时间
#define TURN_PAUSE_MS     4   // 转向断电滑行时间
#define LEFT_TRIM_MAX_MS  8   // 左轮差速校准最大值
#define LEFT_TRIM_MAX_HALF_MS  (LEFT_TRIM_MAX_MS * 2)

#define FOSC_HZ      11059200UL
#define TIMER_RATE   (FOSC_HZ / 12UL)
#define BUZZER_OFF   1
#define GAP_MS       20

#define REST 0
#define G3   196
#define C4   262
#define D4   294
#define E4   330
#define F4   349
#define G4   392
#define GS4  415
#define A4   440
#define B4   494
#define BB4  466
#define C5   523
#define CS5  554
#define D5   587
#define DS5  622
#define E5   659
#define F5   698
#define FS5  740
#define G5   784
#define GS5  831
#define A5   880
#define B5   988
#define C6   1047

#define N16  116
#define N8   232
#define N4   464
#define N2   928

// ===================== 全局状态变量 =====================
bit car_enabled = 0;          // 1: 处于自动循迹模式
bit manual_control = 0;       // 1: 处于蓝牙遥控模式
bit bluetooth_cmd_ready = 0;  // 串口接收完成标志
unsigned char bluetooth_cmd = 0;
unsigned char manual_action = 'S';
unsigned char left_trim_half_ms = 3; // 默认微调值
bit left_trim_extra_toggle = 0;
volatile bit tone_on = 0;
bit music_playing = 0;
volatile unsigned int timer0_reload = 0;

typedef struct {
    unsigned int freq;
    unsigned int ms;
} Note;

Note code song[] = {
    {A4, N4}, {REST, N8}, {A4, N16}, {A4, N16}, {A4, N16}, {A4, N16},
    {A4, N8}, {G4, N8}, {A4, N8}, {REST, N8}, {A4, N16}, {A4, N16}, {A4, N16}, {A4, N16},
    {A4, N8}, {G4, N8}, {A4, N8}, {REST, N8}, {A4, N16}, {A4, N16}, {A4, N16}, {A4, N16},

    {A4, N8}, {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16},
    {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16},

    {A4, N8}, {E5, N4}, {A4, N8}, {A4, N16}, {B4, N16}, {CS5, N16}, {D5, N16},
    {E5, N4}, {REST, N8}, {E5, N16}, {E5, N16}, {F5, N16}, {G5, N16},

    {A5, N4}, {REST, N8}, {A5, N16}, {A5, N16}, {G5, N16}, {F5, N16},
    {G5, N4}, {F5, N16}, {E5, N4}, {E5, N8},
    {D5, N16}, {D5, N16}, {E5, N16}, {F5, N4}, {E5, N16}, {D5, N16},
    {C5, N16}, {C5, N16}, {D5, N16}, {E5, N4}, {D5, N16}, {C5, N16},

    {B4, N16}, {B4, N16}, {CS5, N16}, {DS5, N4}, {FS5, N8},
    {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16},
    {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16},

    {A4, N8}, {E5, N4}, {A4, N8}, {A4, N16}, {B4, N16}, {CS5, N16}, {D5, N16},
    {E5, N4}, {REST, N8}, {E5, N16}, {E5, N16}, {F5, N16}, {G5, N16},
    {A5, N4}, {REST, N8}, {A5, N16}, {A5, N16}, {G5, N16}, {F5, N16},
    {G5, N4}, {F5, N16}, {E5, N4}, {E5, N8},

    {D5, N16}, {D5, N16}, {E5, N16}, {F5, N4}, {E5, N16}, {D5, N16},
    {C5, N16}, {C5, N16}, {D5, N16}, {E5, N4}, {D5, N16}, {C5, N16},
    {B4, N16}, {B4, N16}, {CS5, N16}, {DS5, N4}, {FS5, N8},

    {E5, N4}, {REST, N8}, {E5, N16}, {E5, N16}, {F5, N16}, {G5, N16},
    {A5, N4}, {REST, N8}, {C6, N4},
    {B5, N8}, {GS5, N8}, {E5, N4},
    {F5, N2}, {A5, N8},
    {GS5, N8}, {E5, N4}, {E5, N8},

    {F5, N2}, {A5, N8},
    {GS5, N8}, {E5, N4}, {CS5, N8},
    {D5, N2}, {F5, N8},
    {E5, N8}, {C5, N4}, {A4, N8},
    {B4, N16}, {B4, N16}, {CS5, N16}, {DS5, N4}, {FS5, N8},

    {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16},
    {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16}, {E4, N16},
    {REST, 0}
};

void car_stop();
void play_song();
bit music_check_pause();
bit music_delay_ms(unsigned int ms);

// ===================== 基础延时 =====================
void delay_ms(unsigned int n) {
    unsigned int i, k;
    for (k = n; k > 0; k--)
        for (i = 0; i < 113; i++);
}

// ===================== 蜂鸣器音乐：Timer0 产生方波 =====================
void timer0_load() {
    TH0 = (unsigned char)(timer0_reload >> 8);
    TL0 = (unsigned char)(timer0_reload & 0xFF);
}

void stop_tone() {
    TR0 = 0;
    tone_on = 0;
    BUZZER = BUZZER_OFF;
}

void start_tone(unsigned int freq) {
    unsigned long half_ticks;

    if (freq == REST) {
        stop_tone();
        return;
    }

    half_ticks = TIMER_RATE / ((unsigned long)freq * 2UL);
    if (half_ticks < 1) { half_ticks = 1; }
    if (half_ticks > 65535UL) { half_ticks = 65535UL; }

    ET0 = 0;
    timer0_reload = (unsigned int)(65536UL - half_ticks);
    BUZZER = 0;
    tone_on = 1;
    timer0_load();
    TF0 = 0;
    ET0 = 1;
    TR0 = 1;
}





bit music_check_pause() {
    if (bluetooth_cmd_ready && (bluetooth_cmd == 'M' || bluetooth_cmd == 'm')) {
        bluetooth_cmd_ready = 0;
        stop_tone();
        return 1;
    }
    return 0;
}

bit music_delay_ms(unsigned int ms) {
    unsigned char step;

    while (ms > 0) {
        if (music_check_pause()) {
            return 1;
        }

        if (ms > 10) {
            step = 10;
        }
        else {
            step = (unsigned char)ms;
        }

        delay_ms(step);
        ms -= step;
    }

    return music_check_pause();
}

bit play_note(unsigned int freq, unsigned int ms) {
    start_tone(freq);
    if (music_delay_ms(ms)) {
        stop_tone();
        return 1;
    }

    stop_tone();
    if (music_delay_ms(GAP_MS)) {
        return 1;
    }

    return 0;
}

void play_song() {
    unsigned char i = 0;

    music_playing = 1;
    while (song[i].ms != 0) {
        if (play_note(song[i].freq, song[i].ms)) {
            break;
        }
        i++;
    }
    stop_tone();
    music_playing = 0;
}

void timer0_init() {
    TMOD = (TMOD & 0xF0) | 0x01; // Timer0 mode 1, keep Timer1 bits
    TR0 = 0;
    ET0 = 1;
    BUZZER = BUZZER_OFF;
}

// ===================== 单个电机动作底层 =====================
void right_motor_forward() { RIGHT_MOTOR_A = 0; RIGHT_MOTOR_B = 1; }
void right_motor_backward() { RIGHT_MOTOR_A = 1; RIGHT_MOTOR_B = 0; }
void right_motor_stop() { RIGHT_MOTOR_A = 0; RIGHT_MOTOR_B = 0; }
void left_motor_forward() { LEFT_MOTOR_A = 0; LEFT_MOTOR_B = 1; }
void left_motor_backward() { LEFT_MOTOR_A = 1; LEFT_MOTOR_B = 0; }
void left_motor_stop() { LEFT_MOTOR_A = 0; LEFT_MOTOR_B = 0; }

// ===================== 小车整体动作与调速逻辑 =====================
unsigned char get_left_trim_ms() {
    unsigned char trim_ms = left_trim_half_ms / 2;
    if ((left_trim_half_ms % 2) != 0) {
        left_trim_extra_toggle = !left_trim_extra_toggle;
        if (left_trim_extra_toggle) { trim_ms++; }
    }
    return trim_ms;
}

// 带有软件防跑偏微调的直行函数
void car_forward_trimmed() {
    unsigned char common_run_ms;
    unsigned char trim_ms = get_left_trim_ms();

    if (trim_ms >= STRAIGHT_RUN_MS) { common_run_ms = 1; }
    else { common_run_ms = STRAIGHT_RUN_MS - trim_ms; }

    left_motor_forward();
    right_motor_forward();
    delay_ms(common_run_ms);

    if (trim_ms > 0) {
        left_motor_stop();
        right_motor_forward();
        delay_ms(trim_ms);
    }
    car_stop();
    delay_ms(STRAIGHT_PAUSE_MS);


}

void car_backward_trimmed() {
    unsigned char common_run_ms;
    unsigned char trim_ms = get_left_trim_ms();

    if (trim_ms >= STRAIGHT_RUN_MS) { common_run_ms = 1; }
    else { common_run_ms = STRAIGHT_RUN_MS - trim_ms; }

    left_motor_backward();
    right_motor_backward();
    delay_ms(common_run_ms);

    if (trim_ms > 0) {
        left_motor_stop();
        right_motor_backward();
        delay_ms(trim_ms);
    }
    car_stop();
    delay_ms(STRAIGHT_PAUSE_MS);


}

void car_spin_left() { left_motor_backward(); right_motor_forward(); } // 原地差速左转
void car_spin_right() { left_motor_forward();  right_motor_backward(); }// 原地差速右转
void car_stop() { left_motor_stop();     right_motor_stop(); }

// 转向时的速度限制（软件PWM）
void motor_turn_speed_limit() {
    delay_ms(TURN_RUN_MS);
    car_stop();
    delay_ms(TURN_PAUSE_MS);
}

// ===================== 核心：前置主动避障逻辑 =====================
/*

检查前方障碍并强制接管底盘控制权。

返回 1 表示遇到障碍正在处理，返回 0 表示前方安全。
*/
bit check_and_handle_obstacle() {
    // 障碍物探头：0表示检测到障碍，1表示安全
    bit left_obs = (LEFT_FRONT_IR == 0);
    bit right_obs = (RIGHT_FRONT_IR == 0);

    // 1. 前方安全，不干预，返回0
    if (!left_obs && !right_obs) {
        return 0;
    }

    // --- 一旦发现障碍，立刻切断原本动力 ---
    car_stop();

    // 2. 正前方堵死 (两边都探测到障碍)
    if (left_obs && right_obs) {
        // 一直向后退，直到两边探头都识别不到障碍为止
        while (LEFT_FRONT_IR == 0 || RIGHT_FRONT_IR == 0) {
            car_backward_trimmed(); // 双轮全速倒车
            delay_ms(15);   // 延时滤波防抖
        }
        // 退到安全距离后，强行向右旋转掉头
        car_spin_right();
        delay_ms(300); // 【调试点】修改这个延时可以改变掉头转的角度
        car_stop();
    }
    // 3. 只有左前方有障碍，说明碰到了左侧墙壁
    else if (left_obs) {
        // 向右侧原地转动修正
        car_spin_right();
        delay_ms(150); // 【调试点】修改此延时改变蹭墙反弹的幅度
        car_stop();
    }
    // 4. 只有右前方有障碍，说明碰到了右侧墙壁
    else if (right_obs) {
        // 向左侧原地转动修正
        car_spin_left();
        delay_ms(150);
        car_stop();
    }

    return 1; // 障碍处理完毕，报告接管状态
}

// ===================== 向下循迹逻辑 =====================
bit right_ir_on_line() { return RIGHT_IR == IR_ON_LINE_LEVEL; }
bit left_ir_on_line() { return LEFT_IR == IR_ON_LINE_LEVEL; }

void infrared_follow() {
    bit right_on_line = right_ir_on_line();
    bit left_on_line = left_ir_on_line();

    if (left_on_line && right_on_line) { car_forward_trimmed(); } // 直行
    else if (left_on_line && !right_on_line) { car_spin_right(); motor_turn_speed_limit(); } // 左偏修正
    else if (!left_on_line && right_on_line) { car_spin_left(); motor_turn_speed_limit(); }  // 右偏修正
    else { car_stop(); }


}

// ===================== 物理按键与蓝牙控制 =====================
void wait_s4_release() {
    while (KEY_S4 == KEY_PRESSED_LEVEL) { delay_ms(5); }
}

void scan_s4_key() {
    if (KEY_S4 == KEY_PRESSED_LEVEL) {
        delay_ms(KEY_DEBOUNCE_MS);
        if (KEY_S4 == KEY_PRESSED_LEVEL) {
            car_enabled = !car_enabled; // 切换循迹模式状态
            manual_control = 0;         // 清除蓝牙模式
            if (!car_enabled) { car_stop(); }
            wait_s4_release();
        }
    }
}

void manual_drive_step() {
    switch (manual_action) {
    case 'F': car_forward_trimmed(); break;
    case 'B': car_backward_trimmed(); break;
    case 'L': car_spin_left(); motor_turn_speed_limit(); break;
    case 'R': car_spin_right(); motor_turn_speed_limit(); break;
    default:  car_stop(); break;
    }
}

void uart_init() {
    SCON = 0x50; // 串口方式1，允许接收
    TMOD = (TMOD & 0x0F) | 0x20; // Timer1方式2
    TH1 = 0xFD;  // 9600波特率
    TL1 = 0xFD;
    TR1 = 1;     // 启动定时器1
    ES = 1;      // 开启串口中断
    EA = 1;      // 开启总中断
}

void handle_bluetooth_cmd() {
    if (!bluetooth_cmd_ready) return;
    bluetooth_cmd_ready = 0;

    switch (bluetooth_cmd) {
    case 'F': case 'f': car_enabled = 0; manual_control = 1; manual_action = 'F'; break;
    case 'B': case 'b': car_enabled = 0; manual_control = 1; manual_action = 'B'; break;
    case 'L': case 'l': car_enabled = 0; manual_control = 1; manual_action = 'L'; break;
    case 'R': case 'r': car_enabled = 0; manual_control = 1; manual_action = 'R'; break;
    case 'S': case 's': car_enabled = 0; manual_control = 0; manual_action = 'S'; car_stop(); break;
    case 'A': case 'a': manual_control = 0; car_enabled = 1; manual_action = 'S'; break;
    case 'M': case 'm':
        if (music_playing) {
            stop_tone();
            music_playing = 0;
        }
        else {
            car_enabled = 0;
            manual_control = 0;
            manual_action = 'S';
            car_stop();
            play_song();
        }
        break;
    case '+': if (left_trim_half_ms < LEFT_TRIM_MAX_HALF_MS) left_trim_half_ms++; break;
    case '-': if (left_trim_half_ms > 0) left_trim_half_ms--; break;
    case '0': left_trim_half_ms = 0; break;
    default: break;
    }


}

// Timer0 中断：蜂鸣器方波
void timer0_isr() interrupt 1 {
    if (tone_on) {
        timer0_load();
        BUZZER = !BUZZER;   
    }
}

// 串口中断服务函数
void uart_isr() interrupt 4 {
    if (RI) {
        RI = 0;
        bluetooth_cmd = SBUF;
        bluetooth_cmd_ready = 1;
    }
    if (TI) { TI = 0; }
}

// ===================== 主函数 =====================
void main() {
    P1 = 0xFF; // 全置 1，使引脚(P1.4~P1.7)处于输入探测状态
    KEY_S4 = 1;
    BUZZER = BUZZER_OFF;

    car_stop();
    timer0_init();
    uart_init();
    delay_ms(500);

    while (1) {
        scan_s4_key();
        handle_bluetooth_cmd();

        /*
         * 拦截级：避障最高优先级检测
         * 如果检测到障碍物并进行了处理，check_and_handle_obstacle 会返回 1。
         * 此时 continue 会直接跳过后面的循迹和蓝牙逻辑，强制开始下一次循环检测。
         */
        if (check_and_handle_obstacle()) {
            continue;
        }

        /*
         * 常规级：只有在前方绝对安全时，才会执行以下逻辑
         */
        if (car_enabled) {
            infrared_follow();     // 自动循迹
        }
        else if (manual_control) {
            manual_drive_step();   // 蓝牙遥控
        }
        else if (!manual_control) {
            car_stop();            // 停车待机
        }
    }


}
