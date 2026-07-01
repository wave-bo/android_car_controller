package com.ajddwbo.carcontroller

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import java.io.IOException
import java.io.OutputStream
import java.util.UUID
import kotlin.concurrent.thread
import kotlin.math.max
import kotlin.math.min
import kotlin.math.abs
import android.app.AlertDialog
import android.widget.EditText
import org.json.JSONArray
import org.json.JSONObject
import android.text.InputFilter
class MainActivity : Activity() {
    private val sppUuid: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    private val defaultControls  = listOf(
        Control("f","F", "前进", true, 135, 30),
        Control("l","L", "左转", true, 30, 30),
        Control("s","S", "停止", false, 135, 145),
        Control("r","R", "右转", true, 240, 30),
        Control("b","B", "后退", true, 135, 260),
        Control("a","A", "循迹", false, 30, 390),
        Control("m","M", "音乐", false, 240, 390),
        Control("q","Q", "前进左转", true, 30, 145),
        Control("e","E", "前进右转", true, 240, 145),
        Control("plus","+", "加速", false, 240, 310),
        Control("minus","-", "减速", false, 30, 310),
        Control("zero","0", "速度归零", false, 135, 350),
    )

    private lateinit var statusText: TextView
    private lateinit var deviceSpinner: Spinner
    private lateinit var editMode: CheckBox
    private lateinit var panel: FrameLayout
    private lateinit var sizeSeek: SeekBar

    private val controls = mutableListOf<Control>()
    private val buttons = mutableMapOf<String, Button>()
    private val devices = mutableListOf<BluetoothDevice>()
    private var selectedDevice: BluetoothDevice? = null
    private var socket: BluetoothSocket? = null
    private var output: OutputStream? = null
    private val prefs by lazy { getSharedPreferences("layout", MODE_PRIVATE) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildUi()
        ensureBluetoothPermission()
        loadPairedDevices()
    }

    override fun onDestroy() {
        closeConnection()
        super.onDestroy()
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(16))
            setBackgroundColor(Color.rgb(246, 248, 250))
        }

        statusText = TextView(this).apply {
            text = "未连接"
            textSize = 16f
            setTextColor(Color.rgb(36, 41, 47))
        }
        root.addView(statusText, LinearLayout.LayoutParams(-1, -2))

        deviceSpinner = Spinner(this)
        root.addView(deviceSpinner, LinearLayout.LayoutParams(-1, dp(48)))

        val topButtons = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }

        topButtons.addView(makeActionButton("刷新") { loadPairedDevices() }, weightParams())
        topButtons.addView(makeActionButton("连接") { connectSelectedDevice() }, weightParams())
        topButtons.addView(makeActionButton("断开") { closeConnection() }, weightParams())
        topButtons.addView(makeActionButton("新增按键") {
            showEditControlDialog(null)
        }, weightParams())
        root.addView(topButtons, LinearLayout.LayoutParams(-1, dp(52)))

        editMode = CheckBox(this).apply {
            text = "编辑布局：打开后可拖动按钮"
            textSize = 15f
        }
        root.addView(editMode, LinearLayout.LayoutParams(-1, dp(44)))

        val sizeRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        sizeRow.addView(TextView(this).apply { text = "按钮大小" }, LinearLayout.LayoutParams(dp(78), -2))
        sizeSeek = SeekBar(this).apply {
            max = 80
            progress = prefs.getInt("button_size", 72) - 48
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    val size = progress + 48
                    prefs.edit().putInt("button_size", size).apply()
                    resizeButtons()
                }

                override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
            })
        }
        sizeRow.addView(sizeSeek, LinearLayout.LayoutParams(0, -2, 1f))
        root.addView(sizeRow, LinearLayout.LayoutParams(-1, dp(48)))

        root.addView(makeActionButton("恢复默认按键") {
            resetLayout()
        }, LinearLayout.LayoutParams(-1, dp(46)))

        panel = FrameLayout(this).apply {
            setBackgroundColor(Color.WHITE)
        }
        root.addView(panel, LinearLayout.LayoutParams(-1, dp(520)))

        controls.clear()
        controls.addAll(loadControls())
        renderControls()
        val scroll = ScrollView(this)
        scroll.addView(root)
        setContentView(scroll)
    }

    private fun makeActionButton(textValue: String, action: () -> Unit): Button {
        return Button(this).apply {
            text = textValue
            setOnClickListener { action() }
        }
    }

    private fun weightParams(): LinearLayout.LayoutParams {
        return LinearLayout.LayoutParams(0, -1, 1f).apply {
            marginStart = dp(3)
            marginEnd = dp(3)
        }
    }

    private fun addControlButton(control: Control) {
        val button = Button(this).apply {
            text = control.label
            textSize = 16f
            setTextColor(Color.WHITE)
            setBackgroundResource(com.ajddwbo.carcontroller.R.drawable.control_button)
        }
        buttons[control.id] = button

        val size = buttonSizePx()
        panel.addView(button, FrameLayout.LayoutParams(size, size))
        button.x = dp(control.x).toFloat()
        button.y = dp(control.y).toFloat()

        installButtonTouch(button, control)
    }

    private fun installButtonTouch(button: Button, control: Control) {
        var downX = 0f
        var downY = 0f
        var startX = 0f
        var startY = 0f
        var moved = false
        var sentDown = false

        button.setOnTouchListener { view, event ->
            if (editMode.isChecked) {
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        downX = event.rawX
                        downY = event.rawY
                        startX = view.x
                        startY = view.y
                        moved = false
                        true
                    }

                    MotionEvent.ACTION_MOVE -> {
                        val deltaX = event.rawX - downX
                        val deltaY = event.rawY - downY
                        if (abs(deltaX) > dp(4) || abs(deltaY) > dp(4)) {
                            moved = true
                        }
                        if (moved) {
                            val nextX = startX + deltaX
                            val nextY = startY + deltaY
                            view.x = clamp(nextX, 0f, (panel.width - view.width).toFloat())
                            view.y = clamp(nextY, 0f, (panel.height - view.height).toFloat())
                        }
                        true
                    }

                    MotionEvent.ACTION_UP -> {
                        if (moved) {
                            control.x = (view.x / resources.displayMetrics.density).toInt()
                            control.y = (view.y / resources.displayMetrics.density).toInt()
                            saveControls()
                        } else {
                            showEditControlDialog(control)
                        }
                        true
                    }

                    MotionEvent.ACTION_CANCEL -> {
                        if (moved) {
                            control.x = (view.x / resources.displayMetrics.density).toInt()
                            control.y = (view.y / resources.displayMetrics.density).toInt()
                            saveControls()
                        }
                        true
                    }

                    else -> true
                }
            } else {
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        sentDown = true
                        sendCommand(control.command)
                        true
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        if (sentDown && control.momentary) {
                            sendCommand("S")
                        }
                        sentDown = false
                        view.performClick()
                        true
                    }

                    else -> true
                }
            }
        }
    }
    private fun resizeButtons() {
        val size = buttonSizePx()
        buttons.values.forEach { button ->
            button.layoutParams = FrameLayout.LayoutParams(size, size)
        }
    }

    private fun resetLayout() {
        controls.clear()
        controls.addAll(defaultControls.map { it.copy() })

        saveControls()
        renderControls()
    }
    private fun showEditControlDialog(control: Control?) {
        val isNew = control == null

        val labelInput = EditText(this).apply {
            hint = "按钮文字，例如 前进"
            setText(control?.label ?: "")
        }

        val commandInput = EditText(this).apply {
            hint = "发送字符，例如 F"
            setText(control?.command ?: "")
            filters = arrayOf(InputFilter.LengthFilter(1))
        }

        val momentaryCheck = CheckBox(this).apply {
            text = "点动按钮：按下发送，松开发 S"
            isChecked = control?.momentary ?: false
        }

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(12), dp(20), dp(4))
            addView(labelInput)
            addView(commandInput)
            addView(momentaryCheck)
        }

        val builder = AlertDialog.Builder(this)
            .setTitle(if (isNew) "新增按键" else "编辑按键")
            .setView(layout)
            .setPositiveButton("保存", null)
            .setNegativeButton("取消", null)

        if (!isNew) {
            builder.setNeutralButton("删除") { _, _ ->
                controls.remove(control)
                saveControls()
                renderControls()
            }
        }

        val dialog = builder.create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val label = labelInput.text.toString().trim()
                val command = commandInput.text.toString().trim()

                if (label.isEmpty()) {
                    toast("按钮文字不能为空")
                    return@setOnClickListener
                }

                if (!isValidCommand(command)) {
                    toast("发送字符必须是一个英文/数字/符号")
                    return@setOnClickListener
                }

                if (isNew) {
                    controls.add(
                        Control(
                            id = System.currentTimeMillis().toString(),
                            command = command,
                            label = label,
                            momentary = momentaryCheck.isChecked,
                            x = 135,
                            y = 430
                        )
                    )
                } else {
                    control!!.label = label
                    control.command = command
                    control.momentary = momentaryCheck.isChecked
                }

                saveControls()
                renderControls()
                dialog.dismiss()
            }
        }
        dialog.show()
    }
    private fun ensureBluetoothPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), 100)
        }
    }

    private fun loadPairedDevices() {
        if (!hasBluetoothPermission()) {
            statusText.text = "需要蓝牙权限"
            return
        }

        val adapter = BluetoothAdapter.getDefaultAdapter()
        if (adapter == null) {
            statusText.text = "这台手机不支持蓝牙"
            return
        }

        if (!adapter.isEnabled) {
            statusText.text = "请先打开手机蓝牙"
        }

        devices.clear()
        devices.addAll(adapter.bondedDevices.sortedBy { it.name ?: it.address })

        val names = devices.map { device ->
            val name = device.name ?: "未知设备"
            "$name (${device.address})"
        }

        deviceSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, names)
        deviceSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                selectedDevice = devices.getOrNull(position)
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {
                selectedDevice = null
            }
        }

        statusText.text = if (devices.isEmpty()) {
            "没有已配对设备，请先在系统蓝牙里配对 HC-05/HC-06"
        } else {
            "请选择蓝牙设备"
        }
    }

    private fun connectSelectedDevice() {
        if (!hasBluetoothPermission()) {
            toast("需要蓝牙权限")
            return
        }

        val device = selectedDevice
        if (device == null) {
            toast("请先选择设备")
            return
        }

        statusText.text = "正在连接 ${device.name ?: device.address}..."
        closeConnection(false)

        thread {
            try {
                val newSocket = device.createRfcommSocketToServiceRecord(sppUuid)
                newSocket.connect()
                socket = newSocket
                output = newSocket.outputStream
                runOnUiThread {
                    statusText.text = "已连接 ${device.name ?: device.address}"
                }
            } catch (e: IOException) {
                closeConnection(false)
                runOnUiThread {
                    statusText.text = "连接失败：${e.message ?: "未知错误"}"
                }
            }
        }
    }

    private fun sendCommand(command: String) {
        val stream = output
        if (stream == null) {
            toast("未连接蓝牙")
            return
        }

        thread {
            try {
                stream.write(command.toByteArray(Charsets.US_ASCII))
                stream.flush()
            } catch (e: IOException) {
                runOnUiThread {
                    statusText.text = "发送失败，连接已断开"
                    closeConnection(false)
                }
            }
        }
    }

    private fun closeConnection(showStatus: Boolean = true) {
        try {
            output?.close()
        } catch (_: IOException) {
        }
        try {
            socket?.close()
        } catch (_: IOException) {
        }
        output = null
        socket = null
        if (showStatus) {
            statusText.text = "已断开"
        }
    }

    private fun hasBluetoothPermission(): Boolean {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
    }

    private fun buttonSizePx(): Int {
        return dp(sizeSeek.progress + 48)
    }

    private fun dp(value: Int): Int {
        return (value * resources.displayMetrics.density).toInt()
    }

    private fun clamp(value: Float, minValue: Float, maxValue: Float): Float {
        return max(minValue, min(value, maxValue))
    }
    private fun isValidCommand(command: String): Boolean {
        return command.length == 1 && command[0].code in 32..126
    }


    private fun toast(message: String) {
        runOnUiThread {
            Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
        }
    }


    private fun loadControls(): List<Control> {
        val jsonText = prefs.getString("controls_json", null)

        if (jsonText.isNullOrBlank()) {
            return defaultControls.map { it.copy() }
        }

        return try {
            val array = JSONArray(jsonText)
            val result = mutableListOf<Control>()

            for (i in 0 until array.length()) {
                val obj = array.getJSONObject(i)

                result.add(
                    Control(
                        id = obj.getString("id"),
                        command = obj.getString("command"),
                        label = obj.getString("label"),
                        momentary = obj.getBoolean("momentary"),
                        x = obj.getInt("x"),
                        y = obj.getInt("y")
                    )
                )
            }

            result
        } catch (_: Exception) {
            defaultControls.map { it.copy() }
        }
    }
    private fun renderControls() {
        panel.removeAllViews()
        buttons.clear()

        controls.forEach { control ->
            addControlButton(control)
        }
    }
    private fun saveControls() {
        val array = JSONArray()

        controls.forEach { control ->
            val obj = JSONObject()

            obj.put("id", control.id)
            obj.put("command", control.command)
            obj.put("label", control.label)
            obj.put("momentary", control.momentary)
            obj.put("x", control.x)
            obj.put("y", control.y)

            array.put(obj)
        }

        prefs.edit()
            .putString("controls_json", array.toString())
            .apply()
    }
    private data class Control(
        val id: String,
        var command: String,
        var label: String,
        var momentary: Boolean,
        var x: Int,
        var y: Int
    )
}
