/*
 * 智能小车主程序
 * 功能：蓝牙遥控、自动循迹、前方避障、蜂鸣器音乐。
 * 命令：F/B 前进后退，L/R 原地转，Q/E 前进转弯，S 停车，A 循迹，M 音乐，+/-/0 调整体速度。
 */

#include <reg52.h>

// 电机引脚
sbit RIGHT_MOTOR_A = P1 ^ 0;  // 右电机A
sbit RIGHT_MOTOR_B = P1 ^ 1;  // 右电机B
sbit LEFT_MOTOR_A = P1 ^ 2;   // 左电机A
sbit LEFT_MOTOR_B = P1 ^ 3;   // 左电机B

// 红外探头与其他IO
// 向下探头：循迹
sbit RIGHT_IR = P1 ^ 4;       // 右循迹
sbit LEFT_IR = P1 ^ 5;        // 左循迹

// 向前探头：避障
sbit LEFT_FRONT_IR = P1 ^ 6;  // 左前避障
sbit RIGHT_FRONT_IR = P1 ^ 7; // 右前避障

sbit KEY_S4 = P3 ^ 2;         // S4：切换循迹
sbit BUZZER = P3 ^ 6;         // 蜂鸣器BUZ1

// 参数配置
#define IR_ON_LINE_LEVEL  0   // 黑线有效电平，不对就改成1
#define KEY_PRESSED_LEVEL 0
#define KEY_DEBOUNCE_MS   20
#define STRAIGHT_RUN_MS   10  // 直走通电时间
#define STRAIGHT_PAUSE_MS 6   // 直走断电时间
#define TURN_RUN_MS       10  // 转向通电时间
#define TURN_PAUSE_MS     4   // 转向断电时间
#define LEFT_TRIM_MS      2   // 左轮少跑一点，用来修正跑偏
#define SPEED_PAUSE_MIN_MS 1
#define SPEED_PAUSE_MAX_MS 15

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

// 运行状态
bit car_enabled = 0;          // 自动循迹模式
bit manual_control = 0;       // 蓝牙遥控模式
bit bluetooth_cmd_ready = 0;  // 收到蓝牙字符
unsigned char bluetooth_cmd = 0;
unsigned char manual_action = 'S';
unsigned char straight_pause_ms = STRAIGHT_PAUSE_MS;
unsigned char turn_pause_ms = TURN_PAUSE_MS;
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

// 毫秒延时
void delay_ms(unsigned int n) {
    unsigned int i, k;
    for (k = n; k > 0; k--)
        for (i = 0; i < 113; i++);
}

// 蜂鸣器音乐：Timer0产生方波，Timer1留给串口
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

// 单个电机动作
void right_motor_forward() { RIGHT_MOTOR_A = 0; RIGHT_MOTOR_B = 1; }
void right_motor_backward() { RIGHT_MOTOR_A = 1; RIGHT_MOTOR_B = 0; }
void right_motor_stop() { RIGHT_MOTOR_A = 0; RIGHT_MOTOR_B = 0; }
void left_motor_forward() { LEFT_MOTOR_A = 0; LEFT_MOTOR_B = 1; }
void left_motor_backward() { LEFT_MOTOR_A = 1; LEFT_MOTOR_B = 0; }
void left_motor_stop() { LEFT_MOTOR_A = 0; LEFT_MOTOR_B = 0; }

// 小车动作与软件调速
void car_forward_step() {
    left_motor_forward();
    right_motor_forward();
    delay_ms(STRAIGHT_RUN_MS - LEFT_TRIM_MS);
    left_motor_stop();
    right_motor_forward();
    delay_ms(LEFT_TRIM_MS);
    car_stop();
    delay_ms(straight_pause_ms);
}

void car_backward_step() {
    left_motor_backward();
    right_motor_backward();
    delay_ms(STRAIGHT_RUN_MS - LEFT_TRIM_MS);
    left_motor_stop();
    right_motor_backward();
    delay_ms(LEFT_TRIM_MS);
    car_stop();
    delay_ms(straight_pause_ms);
}

void car_forward_left_step() {
    left_motor_stop();
    right_motor_forward();
    delay_ms(TURN_RUN_MS);
    car_stop();
    delay_ms(turn_pause_ms);
}

void car_forward_right_step() {
    left_motor_forward();
    right_motor_stop();
    delay_ms(TURN_RUN_MS);
    car_stop();
    delay_ms(turn_pause_ms);
}
void car_spin_left() { left_motor_backward(); right_motor_forward(); } // 原地左转
void car_spin_right() { left_motor_forward();  right_motor_backward(); }// 原地右转
void car_stop() { left_motor_stop();     right_motor_stop(); }

// 转向时的速度限制（软件PWM）
void motor_turn_speed_limit() {
    delay_ms(TURN_RUN_MS);
    car_stop();
    delay_ms(turn_pause_ms);
}

// 前方避障：有障碍时接管小车，返回1；安全返回0。
bit check_and_handle_obstacle() {
    bit left_obs = (LEFT_FRONT_IR == 0);
    bit right_obs = (RIGHT_FRONT_IR == 0);

    if (!left_obs && !right_obs) {
        return 0;
    }

    // 遇到障碍先停车。
    car_stop();

    // 两边都有障碍：后退，再右转。
    if (left_obs && right_obs) {
        while (LEFT_FRONT_IR == 0 || RIGHT_FRONT_IR == 0) {
            car_backward_step();
            delay_ms(15);
        }
        car_spin_right();
        delay_ms(300); // 调整掉头角度
        car_stop();
    }
    // 左侧有障碍：向右修正。
    else if (left_obs) {
        car_spin_right();
        delay_ms(150);
        car_stop();
    }
    // 右侧有障碍：向左修正。
    else if (right_obs) {
        car_spin_left();
        delay_ms(150);
        car_stop();
    }

    return 1;
}

// 循迹逻辑
bit right_ir_on_line() { return RIGHT_IR == IR_ON_LINE_LEVEL; }
bit left_ir_on_line() { return LEFT_IR == IR_ON_LINE_LEVEL; }

void infrared_follow() {
    bit right_on_line = right_ir_on_line();
    bit left_on_line = left_ir_on_line();

    if (left_on_line && right_on_line) { car_forward_step(); } // 直行
    else if (left_on_line && !right_on_line) { car_spin_right(); motor_turn_speed_limit(); } // 左偏修正
    else if (!left_on_line && right_on_line) { car_spin_left(); motor_turn_speed_limit(); }  // 右偏修正
    else { car_stop(); }

}

// 按键与蓝牙控制
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
    case 'F': car_forward_step(); break;
    case 'B': car_backward_step(); break;
    case 'L': car_spin_left(); motor_turn_speed_limit(); break;
    case 'R': car_spin_right(); motor_turn_speed_limit(); break;
    case 'Q': car_forward_left_step(); break;
    case 'E': car_forward_right_step(); break;
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
    case 'Q': case 'q': car_enabled = 0; manual_control = 1; manual_action = 'Q'; break;
    case 'E': case 'e': car_enabled = 0; manual_control = 1; manual_action = 'E'; break;
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
    case '+':
        if (straight_pause_ms > SPEED_PAUSE_MIN_MS) straight_pause_ms--;
        if (turn_pause_ms > SPEED_PAUSE_MIN_MS) turn_pause_ms--;
        break;
    case '-':
        if (straight_pause_ms < SPEED_PAUSE_MAX_MS) straight_pause_ms++;
        if (turn_pause_ms < SPEED_PAUSE_MAX_MS) turn_pause_ms++;
        break;
    case '0':
        straight_pause_ms = STRAIGHT_PAUSE_MS;
        turn_pause_ms = TURN_PAUSE_MS;
        break;
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

// 主函数
void main() {
    P1 = 0xFF; // P1.4~P1.7作为传感器输入
    KEY_S4 = 1;
    BUZZER = BUZZER_OFF;

    car_stop();
    timer0_init();
    uart_init();
    delay_ms(500);

    while (1) {
        scan_s4_key();
        handle_bluetooth_cmd();

        // 避障优先级最高。
        if (check_and_handle_obstacle()) {
            continue;
        }

        // 没有障碍时，执行当前模式。
        if (car_enabled) {
            infrared_follow();
        }
        else if (manual_control) {
            manual_drive_step();
        }
        else if (!manual_control) {
            car_stop();
        }
    }

}
