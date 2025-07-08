import requests
import datetime
import time
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import paho.mqtt.client as mqtt
import json
import threading
import random

# 华为云IAM配置信息
IAM_CONFIG = {
    "MAIN_USER": "hid_ajrcxqbs-41_riz",
    "endpoint": "https://iam.cn-north-4.myhuaweicloud.com:443",
    "username": "xtgo",
    "password": "xt12345678",
    "domain": "cn-north-4",
    "project_id": "e35868f675bf4c1fa07205813fdff6bc"  # 使用项目ID
}

# IoT服务配置
IOT_CONFIG = {
    "region": "cn-north-4",  # 与IAM区域一致
    "service_endpoint": "https://9e87ba4f3e.st1.iotda-app.cn-north-4.myhuaweicloud.com:443"
}

# MQTT 配置信息
MQTT_CONFIG = {
    "server": "117.78.5.125",
    "port": 1883,
    "client_id": "6863a00ad582f2001836ef74_dev1_0_0_2025070109",
    "username": "6863a00ad582f2001836ef74_dev1",
    "password": "cd17724805b62eb626d87c0d650b1edc17b5c36343eae6fedc84bdb8099bc81c",
    "subscribe_topic": "$oc/devices/6863a00ad582f2001836ef74_dev1/sys/messages/down",
    "publish_topic": "$oc/devices/6863a00ad582f2001836ef74_dev1/sys/properties/report"
}

# 目标设备ID
TARGET_DEVICE_ID = "6863a00ad582f2001836ef74_dev1"

class HuaweiIoTDeviceMonitor:
    def __init__(self, app):
        self.app = app
        self.token_info = None
        self.last_shadow_data = None
        self.running = False
        self.thread = None
    
    def get_huawei_cloud_token(self):
        """获取华为云IAM访问令牌"""
        try:
            url = f"{IAM_CONFIG['endpoint']}/v3/auth/tokens"
            auth_data = {
                "auth": {
                    "identity": {
                        "methods": ["password"],
                        "password": {
                            "user": {
                                "name": IAM_CONFIG["username"],
                                "password": IAM_CONFIG["password"],
                                "domain": {"name": IAM_CONFIG["MAIN_USER"]}
                            }
                        }
                    },
                    "scope": {
                        "project": {"id": IAM_CONFIG["project_id"]}
                    }
                }
            }
            headers = {"Content-Type": "application/json"}
            response = requests.post(url, json=auth_data, headers=headers)
            
            if response.status_code == 201:
                token = response.headers.get('X-Subject-Token')
                if not token:
                    self.app.log_message("错误: 响应中缺少X-Subject-Token头部")
                    return None
                
                try:
                    token_data = response.json()
                    expires_at = token_data['token']['expires_at']
                    expire_time = datetime.datetime.strptime(expires_at, "%Y-%m-%dT%H:%M:%S.%fZ")
                    return token
                except (KeyError, ValueError) as e:
                    self.app.log_message(f"解析令牌响应失败: {str(e)}")
                    return None
            else:
                self.app.log_message(f"获取Token失败: {response.status_code} - {response.text}")
                return None
        except Exception as e:
            self.app.log_message(f"请求过程中出错: {str(e)}")
            return None
    
    def get_device_shadow(self, device_id):
        """获取指定设备的影子数据"""
        token = self.get_huawei_cloud_token()
        if not token:
            return None
            
        url = f"{IOT_CONFIG['service_endpoint']}/v5/iot/{IAM_CONFIG['project_id']}/devices/{device_id}/shadow"
        headers = {
            "Content-Type": "application/json",
            "X-Auth-Token": token
        }
        
        try:
            response = requests.get(url, headers=headers)
            if response.status_code == 200:
                return response.json()
            else:
                self.app.log_message(f"获取设备影子失败: {response.status_code} - {response.text}")
                return None
        except Exception as e:
            self.app.log_message(f"请求设备影子时出错: {str(e)}")
            return None
    
    def monitor_device_shadow(self):
        """实时监控设备影子数据变化"""
        self.app.log_message("启动设备影子监控...")
        self.running = True
        
        while self.running:
            try:
                # 获取设备影子数据
                shadow_data = self.get_device_shadow(TARGET_DEVICE_ID)
                print(shadow_data)
                
                if shadow_data:
                    shadows = shadow_data.get("shadow", [])
                    if shadows:
                        shadow = shadows[0]
                        reported = shadow.get("reported", {})
                        
                        # 检测变化
                        changed = False
                        if self.last_shadow_data is None:
                            changed = True
                        else:
                            for key, value in reported.items():
                                if key not in self.last_shadow_data or self.last_shadow_data[key] != value:
                                    changed = True
                                    break
                        
                        # 如果有变化或首次获取
                        if changed:
                            self.last_shadow_data = reported.copy()
                            
                            # 在主线程中更新UI
                            self.app.root.after(0, self.app.update_shadow_display, reported.copy())
                
                # 等待下一次查询
                time.sleep(15)  # 每15秒查询一次
                
            except Exception as e:
                self.app.log_message(f"影子监控异常: {str(e)}")
                time.sleep(30)  # 出错后等待30秒再试
    
    def start_monitoring(self):
        """启动监控线程"""
        if not self.running:
            self.thread = threading.Thread(target=self.monitor_device_shadow, daemon=True)
            self.thread.start()
    
    def stop_monitoring(self):
        """停止监控"""
        self.running = False
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=5)
        self.app.log_message("设备影子监控已停止")

class HuaweiCloudTester:
    def __init__(self, root):
        self.root = root
        self.root.title("华为云设备监控平台")
        self.root.geometry("1200x800")
        self.root.resizable(True, True)
        
        # MQTT客户端
        self.mqtt_client = None
        self.connected = False
        self.mqtt_running = False
        
        # 设备影子监控器
        self.shadow_monitor = HuaweiIoTDeviceMonitor(self)
        self.shadow_running = False
        
        # 设备数据存储
        self.device_data = {
            "DHT11_T": 0,
            "DHT11_H": 0,
            "HW": 0,
            "Rain": 0,
            "Light": 0,
            "MQ2": 0,
            "Motor": "STOP"
        }
        
        # 创建主框架
        main_frame = ttk.Frame(root, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # 创建标签页
        self.notebook = ttk.Notebook(main_frame)
        self.notebook.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # 配置标签页
        config_tab = ttk.Frame(self.notebook, padding="10")
        self.notebook.add(config_tab, text="MQTT配置")
        
        # 监控标签页
        monitor_tab = ttk.Frame(self.notebook, padding="10")
        self.notebook.add(monitor_tab, text="设备监控")
        
        # 配置标签页内容
        self.setup_config_tab(config_tab)
        
        # 监控标签页内容
        self.setup_monitor_tab(monitor_tab)
        
        # 关闭窗口时清理资源
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
    
    def setup_config_tab(self, parent):
        """设置配置标签页"""
        # 配置区域
        config_frame = ttk.LabelFrame(parent, text="MQTT配置", padding="10")
        config_frame.pack(fill=tk.X, pady=5)
        
        # 配置项
        ttk.Label(config_frame, text="服务器地址:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=2)
        self.server_var = tk.StringVar(value=MQTT_CONFIG["server"])
        ttk.Entry(config_frame, textvariable=self.server_var, width=25).grid(row=0, column=1, sticky=tk.W, padx=5, pady=2)
        
        ttk.Label(config_frame, text="端口:").grid(row=0, column=2, sticky=tk.W, padx=5, pady=2)
        self.port_var = tk.StringVar(value=str(MQTT_CONFIG["port"]))
        ttk.Entry(config_frame, textvariable=self.port_var, width=8).grid(row=0, column=3, sticky=tk.W, padx=5, pady=2)
        
        ttk.Label(config_frame, text="客户端ID:").grid(row=1, column=0, sticky=tk.W, padx=5, pady=2)
        self.client_id_var = tk.StringVar(value=MQTT_CONFIG["client_id"])
        ttk.Entry(config_frame, textvariable=self.client_id_var, width=25).grid(row=1, column=1, sticky=tk.W, padx=5, pady=2)
        
        ttk.Label(config_frame, text="用户名:").grid(row=2, column=0, sticky=tk.W, padx=5, pady=2)
        self.username_var = tk.StringVar(value=MQTT_CONFIG["username"])
        ttk.Entry(config_frame, textvariable=self.username_var, width=25).grid(row=2, column=1, sticky=tk.W, padx=5, pady=2)
        
        ttk.Label(config_frame, text="密码:").grid(row=3, column=0, sticky=tk.W, padx=5, pady=2)
        self.password_var = tk.StringVar(value=MQTT_CONFIG["password"])
        ttk.Entry(config_frame, textvariable=self.password_var, width=25, show="*").grid(row=3, column=1, sticky=tk.W, padx=5, pady=2)
        
        ttk.Label(config_frame, text="订阅主题:").grid(row=4, column=0, sticky=tk.W, padx=5, pady=2)
        self.sub_topic_var = tk.StringVar(value=MQTT_CONFIG["subscribe_topic"])
        ttk.Entry(config_frame, textvariable=self.sub_topic_var, width=40).grid(row=4, column=1, columnspan=3, sticky=tk.W, padx=5, pady=2)
        
        ttk.Label(config_frame, text="发布主题:").grid(row=5, column=0, sticky=tk.W, padx=5, pady=2)
        self.pub_topic_var = tk.StringVar(value=MQTT_CONFIG["publish_topic"])
        ttk.Entry(config_frame, textvariable=self.pub_topic_var, width=40).grid(row=5, column=1, columnspan=3, sticky=tk.W, padx=5, pady=2)
        
        # 控制按钮
        button_frame = ttk.Frame(parent)
        button_frame.pack(fill=tk.X, pady=10)
        
        self.connect_btn = ttk.Button(button_frame, text="连接MQTT", command=self.connect_mqtt)
        self.connect_btn.pack(side=tk.LEFT, padx=5)
        
        self.disconnect_btn = ttk.Button(button_frame, text="断开MQTT", command=self.disconnect_mqtt, state=tk.DISABLED)
        self.disconnect_btn.pack(side=tk.LEFT, padx=5)
        
        self.shadow_start_btn = ttk.Button(button_frame, text="启动影子监控", command=self.start_shadow_monitor)
        self.shadow_start_btn.pack(side=tk.LEFT, padx=5)
        
        self.shadow_stop_btn = ttk.Button(button_frame, text="停止影子监控", command=self.stop_shadow_monitor, state=tk.DISABLED)
        self.shadow_stop_btn.pack(side=tk.LEFT, padx=5)
        
        self.publish_btn = ttk.Button(button_frame, text="测试发布", command=self.test_publish, state=tk.DISABLED)
        self.publish_btn.pack(side=tk.LEFT, padx=5)
        
        # 状态区域
        status_frame = ttk.Frame(parent)
        status_frame.pack(fill=tk.X, pady=5)
        
        self.status_var = tk.StringVar(value="状态: 未连接")
        ttk.Label(status_frame, textvariable=self.status_var, font=("Arial", 10)).pack(side=tk.LEFT)
        
        # 连接状态指示灯
        self.status_light = tk.Canvas(status_frame, width=20, height=20, bg="white", bd=0, highlightthickness=0)
        self.status_light.pack(side=tk.LEFT, padx=10)
        self.status_light.create_oval(2, 2, 18, 18, fill="red", tags="light")
        
        # 消息区域
        msg_frame = ttk.LabelFrame(parent, text="消息日志", padding="10")
        msg_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        self.msg_text = scrolledtext.ScrolledText(msg_frame, wrap=tk.WORD, width=80, height=15)
        self.msg_text.pack(fill=tk.BOTH, expand=True)
        self.msg_text.config(state=tk.DISABLED)
        
        # 清空日志按钮
        ttk.Button(msg_frame, text="清空日志", command=self.clear_log).pack(side=tk.BOTTOM, pady=5)
    
    def setup_monitor_tab(self, parent):
        """设置设备监控标签页"""
        # 顶部状态栏
        status_frame = ttk.Frame(parent)
        status_frame.pack(fill=tk.X, pady=5)
        
        # 设备信息
        device_info_frame = ttk.LabelFrame(status_frame, text="设备信息", padding="5")
        device_info_frame.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        ttk.Label(device_info_frame, text="设备ID:").grid(row=0, column=0, sticky=tk.W)
        self.device_id_var = tk.StringVar(value=TARGET_DEVICE_ID)
        ttk.Label(device_info_frame, textvariable=self.device_id_var).grid(row=0, column=1, sticky=tk.W, padx=5)
        
        ttk.Label(device_info_frame, text="连接状态:").grid(row=0, column=2, sticky=tk.W, padx=10)
        self.device_status_var = tk.StringVar(value="离线")
        ttk.Label(device_info_frame, textvariable=self.device_status_var, foreground="red").grid(row=0, column=3, sticky=tk.W, padx=5)
        
        # 数据更新时间
        ttk.Label(device_info_frame, text="最后更新时间:").grid(row=0, column=4, sticky=tk.W, padx=10)
        self.last_update_var = tk.StringVar(value="--:--:--")
        ttk.Label(device_info_frame, textvariable=self.last_update_var).grid(row=0, column=5, sticky=tk.W, padx=5)
        
        # 传感器数据展示
        sensor_frame = ttk.LabelFrame(parent, text="实时传感器数据 (MQTT)", padding="10")
        sensor_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # 网格布局
        row = 0
        col = 0
        
        # 温度传感器
        temp_frame = ttk.LabelFrame(sensor_frame, text="温度 (DHT11_T)", padding="5")
        temp_frame.grid(row=row, column=col, padx=5, pady=5, sticky="nsew")
        
        self.temp_var = tk.StringVar(value="0°C")
        ttk.Label(temp_frame, textvariable=self.temp_var, font=("Arial", 14)).pack(pady=5)
        
        self.temp_bar = ttk.Progressbar(temp_frame, orient=tk.HORIZONTAL, length=150, mode='determinate')
        self.temp_bar.pack(pady=5, fill=tk.X)
        self.temp_bar["value"] = 0
        
        # 湿度传感器
        humi_frame = ttk.LabelFrame(sensor_frame, text="湿度 (DHT11_H)", padding="5")
        humi_frame.grid(row=row, column=col+1, padx=5, pady=5, sticky="nsew")
        
        self.humi_var = tk.StringVar(value="0%")
        ttk.Label(humi_frame, textvariable=self.humi_var, font=("Arial", 14)).pack(pady=5)
        
        self.humi_bar = ttk.Progressbar(humi_frame, orient=tk.HORIZONTAL, length=150, mode='determinate')
        self.humi_bar.pack(pady=5, fill=tk.X)
        self.humi_bar["value"] = 0
        
        # 雨量传感器
        rain_frame = ttk.LabelFrame(sensor_frame, text="雨量 (Rain)", padding="5")
        rain_frame.grid(row=row, column=col+2, padx=5, pady=5, sticky="nsew")
        
        self.rain_var = tk.StringVar(value="0")
        ttk.Label(rain_frame, textvariable=self.rain_var, font=("Arial", 14)).pack(pady=5)
        
        self.rain_bar = ttk.Progressbar(rain_frame, orient=tk.HORIZONTAL, length=150, mode='determinate')
        self.rain_bar.pack(pady=5, fill=tk.X)
        self.rain_bar["value"] = 0
        
        row += 1
        
        # 光照传感器
        light_frame = ttk.LabelFrame(sensor_frame, text="光照 (Light)", padding="5")
        light_frame.grid(row=row, column=col, padx=5, pady=5, sticky="nsew")
        
        self.light_var = tk.StringVar(value="0")
        ttk.Label(light_frame, textvariable=self.light_var, font=("Arial", 14)).pack(pady=5)
        
        self.light_bar = ttk.Progressbar(light_frame, orient=tk.HORIZONTAL, length=150, mode='determinate')
        self.light_bar.pack(pady=5, fill=tk.X)
        self.light_bar["value"] = 0
        
        # 气体传感器
        gas_frame = ttk.LabelFrame(sensor_frame, text="气体浓度 (MQ2)", padding="5")
        gas_frame.grid(row=row, column=col+1, padx=5, pady=5, sticky="nsew")
        
        self.gas_var = tk.StringVar(value="0")
        ttk.Label(gas_frame, textvariable=self.gas_var, font=("Arial", 14)).pack(pady=5)
        
        self.gas_bar = ttk.Progressbar(gas_frame, orient=tk.HORIZONTAL, length=150, mode='determinate')
        self.gas_bar.pack(pady=5, fill=tk.X)
        self.gas_bar["value"] = 0
        
        # 设备状态和控制区域
        control_frame = ttk.LabelFrame(parent, text="设备状态与控制", padding="10")
        control_frame.pack(fill=tk.X, pady=5)
        
        # 左侧 - 设备状态
        state_frame = ttk.LabelFrame(control_frame, text="设备状态", padding="10")
        state_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)
        
        # HW开关状态
        hw_frame = ttk.Frame(state_frame)
        hw_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(hw_frame, text="HW开关状态:", width=15).pack(side=tk.LEFT)
        self.hw_state_var = tk.StringVar(value="关闭")
        ttk.Label(hw_frame, textvariable=self.hw_state_var, font=("Arial", 12), 
                 foreground="red").pack(side=tk.LEFT, padx=5)
        
        # 电机状态
        motor_frame = ttk.Frame(state_frame)
        motor_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(motor_frame, text="电机状态:", width=15).pack(side=tk.LEFT)
        self.motor_state_var = tk.StringVar(value="停止")
        ttk.Label(motor_frame, textvariable=self.motor_state_var, font=("Arial", 12), 
                 foreground="red").pack(side=tk.LEFT, padx=5)
        
        # 右侧 - 设备控制
        ctrl_frame = ttk.LabelFrame(control_frame, text="设备控制", padding="10")
        ctrl_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)
        
        # HW开关控制
        hw_ctrl_frame = ttk.Frame(ctrl_frame)
        hw_ctrl_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(hw_ctrl_frame, text="HW开关控制:").pack(side=tk.LEFT)
        self.hw_ctrl_var = tk.StringVar(value="关闭")
        hw_combo = ttk.Combobox(hw_ctrl_frame, textvariable=self.hw_ctrl_var, 
                               values=["开启", "关闭"], state="readonly", width=8)
        hw_combo.pack(side=tk.LEFT, padx=5)
        ttk.Button(hw_ctrl_frame, text="发送", command=self.send_hw_command).pack(side=tk.LEFT)
        
        # 电机控制
        motor_ctrl_frame = ttk.Frame(ctrl_frame)
        motor_ctrl_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(motor_ctrl_frame, text="电机控制:").pack(side=tk.LEFT)
        self.motor_ctrl_var = tk.StringVar(value="停止")
        motor_combo = ttk.Combobox(motor_ctrl_frame, textvariable=self.motor_ctrl_var, 
                                  values=["启动", "停止"], state="readonly", width=8)
        motor_combo.pack(side=tk.LEFT, padx=5)
        ttk.Button(motor_ctrl_frame, text="发送", command=self.send_motor_command).pack(side=tk.LEFT)
        
        # 设备影子数据展示
        shadow_frame = ttk.LabelFrame(parent, text="设备影子数据 (华为云API)", padding="10")
        shadow_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # 创建影子数据显示区域
        shadow_columns = ("属性", "值", "更新时间")
        self.shadow_tree = ttk.Treeview(shadow_frame, columns=shadow_columns, show="headings", height=8)
        
        # 设置列标题
        for col in shadow_columns:
            self.shadow_tree.heading(col, text=col)
            self.shadow_tree.column(col, width=120, anchor=tk.CENTER)
        
        self.shadow_tree.pack(fill=tk.BOTH, expand=True)
        
        # 添加滚动条
        shadow_scrollbar = ttk.Scrollbar(shadow_frame, orient="vertical", command=self.shadow_tree.yview)
        shadow_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.shadow_tree.configure(yscrollcommand=shadow_scrollbar.set)
        
        # 初始化影子数据
        self.shadow_data = {}
        
        # 添加一个刷新按钮
        refresh_btn = ttk.Button(shadow_frame, text="手动刷新影子数据", command=self.manual_refresh_shadow)
        refresh_btn.pack(side=tk.BOTTOM, pady=5)
    
    def log_message(self, message):
        """添加日志消息"""
        self.msg_text.config(state=tk.NORMAL)
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        self.msg_text.insert(tk.END, f"[{timestamp}] {message}\n")
        self.msg_text.see(tk.END)  # 滚动到底部
        self.msg_text.config(state=tk.DISABLED)
    
    def clear_log(self):
        """清空日志"""
        self.msg_text.config(state=tk.NORMAL)
        self.msg_text.delete(1.0, tk.END)
        self.msg_text.config(state=tk.DISABLED)
        self.log_message("日志已清空")
    
    def connect_mqtt(self):
        """连接MQTT服务器"""
        if self.mqtt_client and self.connected:
            self.log_message("已经连接到MQTT服务器")
            return
        
        try:
            # 获取配置
            config = {
                "server": self.server_var.get(),
                "port": int(self.port_var.get()),
                "client_id": self.client_id_var.get(),
                "username": self.username_var.get(),
                "password": self.password_var.get(),
                "subscribe_topic": self.sub_topic_var.get(),
                "publish_topic": self.pub_topic_var.get()
            }
            
            self.log_message("正在连接MQTT服务器...")
            self.status_var.set("状态: 连接中...")
            self.status_light.itemconfig("light", fill="yellow")  # 连接中状态为黄色
            
            # 创建MQTT客户端
            self.mqtt_client = mqtt.Client(
                client_id=config["client_id"], 
                protocol=mqtt.MQTTv311,
                callback_api_version=mqtt.CallbackAPIVersion.VERSION1
            )
            
            # 设置认证信息
            self.mqtt_client.username_pw_set(config["username"], config["password"])
            
            # 设置回调函数
            self.mqtt_client.on_connect = self.on_connect
            self.mqtt_client.on_message = self.on_message
            self.mqtt_client.on_disconnect = self.on_disconnect
            
            # 连接到MQTT服务器
            self.mqtt_client.connect(config["server"], config["port"], 60)
            
            # 启动网络循环
            self.mqtt_client.loop_start()
            self.mqtt_running = True
            
            # 更新按钮状态
            self.connect_btn.config(state=tk.DISABLED)
            self.disconnect_btn.config(state=tk.NORMAL)
            self.publish_btn.config(state=tk.NORMAL)
            
        except ValueError:
            self.log_message("错误: 端口必须是整数")
            self.status_var.set("状态: 配置错误")
            self.status_light.itemconfig("light", fill="red")
        except Exception as e:
            self.log_message(f"连接MQTT失败: {str(e)}")
            self.status_var.set("状态: 连接失败")
            self.status_light.itemconfig("light", fill="red")
    
    def disconnect_mqtt(self):
        """断开MQTT连接"""
        if self.mqtt_client and self.connected:
            try:
                self.mqtt_running = False
                self.mqtt_client.loop_stop()
                self.mqtt_client.disconnect()
                self.connected = False
                self.status_var.set("状态: 已断开")
                self.status_light.itemconfig("light", fill="red")
                self.device_status_var.set("离线")
                self.log_message("已断开MQTT连接")
                
                # 更新按钮状态
                self.connect_btn.config(state=tk.NORMAL)
                self.disconnect_btn.config(state=tk.DISABLED)
                self.publish_btn.config(state=tk.DISABLED)
            except Exception as e:
                self.log_message(f"断开连接失败: {str(e)}")
    
    def start_shadow_monitor(self):
        """启动设备影子监控"""
        if not self.shadow_running:
            self.shadow_monitor.start_monitoring()
            self.shadow_running = True
            self.log_message("设备影子监控已启动")
            self.shadow_start_btn.config(state=tk.DISABLED)
            self.shadow_stop_btn.config(state=tk.NORMAL)
    
    def stop_shadow_monitor(self):
        """停止设备影子监控"""
        if self.shadow_running:
            self.shadow_monitor.stop_monitoring()
            self.shadow_running = False
            self.log_message("设备影子监控已停止")
            self.shadow_start_btn.config(state=tk.NORMAL)
            self.shadow_stop_btn.config(state=tk.DISABLED)
    
    def manual_refresh_shadow(self):
        """手动刷新影子数据"""
        if self.shadow_running:
            self.log_message("正在手动刷新影子数据...")
            shadow_data = self.shadow_monitor.get_device_shadow(TARGET_DEVICE_ID)
            if shadow_data:
                shadows = shadow_data.get("shadow", [])
                if shadows:
                    reported = shadows[0].get("reported", {})
                    self.update_shadow_display(reported)
                    self.log_message("影子数据刷新成功")
        else:
            self.log_message("请先启动影子监控")
    
    def update_shadow_display(self, shadow_data):
        print("start to update shadow display")
        print(shadow_data)
        """更新设备影子数据显示"""
        # 更新影子数据存储
        self.shadow_data = shadow_data.copy()
        
        # 清空现有数据
        self.shadow_tree.delete(*self.shadow_tree.get_children())
        
        # 添加新数据
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        for key, value in shadow_data.items():
            self.shadow_tree.insert("", "end", values=(key, value, timestamp))
            
        self.update_device_display(shadow_data["properties"])
    
    def test_publish(self):
        """测试发布消息"""
        if not self.mqtt_client or not self.connected:
            self.log_message("无法发布数据: 未连接到MQTT")
            return
        
        try:
            # 创建测试消息
            test_payload = {
                "services": [{
                    "service_id": "hi3861", 
                    "properties": {
                        "DHT11_T": random.randint(20, 35),
                        "DHT11_H": random.randint(40, 80),
                        "HW": random.choice([0, 1]),
                        "Rain": random.randint(0, 100),
                        "Light": random.randint(0, 1000),
                        "MQ2": random.randint(0, 3000),
                        "Motor": random.choice(["MOTOR_START", "MOTOR_STOP"])
                    }
                }]
            }
            
            topic = self.pub_topic_var.get()
            
            self.mqtt_client.publish(topic, json.dumps(test_payload))
            self.log_message(f"已发布测试消息到主题: {topic}")
            self.log_message(f"消息内容: {json.dumps(test_payload, indent=2)}")
            
        except Exception as e:
            self.log_message(f"发布测试消息失败: {str(e)}")
    
    def send_hw_command(self):
        """发送HW开关控制命令"""
        if not self.mqtt_client or not self.connected:
            self.log_message("无法发送命令: 未连接到MQTT")
            return
        
        try:
            command = self.hw_ctrl_var.get()
            hw_value = 1 if command == "开启" else 0
            
            control_payload = {
                "services": [{
                    "service_id": "hi3861", 
                    "properties": {
                        "HW": hw_value
                    }
                }]
            }
            
            topic = self.pub_topic_var.get()
            self.mqtt_client.publish(topic, json.dumps(control_payload))
            
            self.log_message(f"已发送HW控制命令: {'开启' if hw_value == 1 else '关闭'}")
            
        except Exception as e:
            self.log_message(f"发送控制命令失败: {str(e)}")
    
    def send_motor_command(self):
        """发送电机控制命令"""
        if not self.mqtt_client or not self.connected:
            self.log_message("无法发送命令: 未连接到MQTT")
            return
        
        try:
            command = self.motor_ctrl_var.get()
            motor_value = "MOTOR_START" if command == "启动" else "MOTOR_STOP"
            
            control_payload = {
                "services": [{
                    "service_id": "hi3861", 
                    "properties": {
                        "Motor": motor_value
                    }
                }]
            }
            
            topic = self.pub_topic_var.get()
            self.mqtt_client.publish(topic, json.dumps(control_payload))
            
            self.log_message(f"已发送电机控制命令: {'启动' if motor_value == 'MOTOR_START' else '停止'}")
            
        except Exception as e:
            self.log_message(f"发送控制命令失败: {str(e)}")
    
    def update_device_display(self, properties):
        """更新设备数据显示"""
        # 更新时间
        self.last_update_var.set(time.strftime("%H:%M:%S"))
        print(properties)
        
        # 更新温度显示
        if "DHT11_T" in properties:
            temp = properties["DHT11_T"]
            self.temp_var.set(f"{temp}°C")
            self.temp_bar["value"] = min(temp, 50) * 2  # 假设最大温度50°C
            self.device_data["DHT11_T"] = temp
        
        # 更新湿度显示
        if "DHT11_H" in properties:
            humi = properties["DHT11_H"]
            self.humi_var.set(f"{humi}%")
            self.humi_bar["value"] = humi
            self.device_data["DHT11_H"] = humi
        
        # 更新雨量显示
        if "Rain" in properties:
            rain = properties["Rain"]
            self.rain_var.set(str(rain))
            self.rain_bar["value"] = rain
            self.device_data["Rain"] = rain
        
        # 更新光照显示
        if "Light" in properties:
            light = properties["Light"]
            self.light_var.set(str(light))
            self.light_bar["value"] = min(light, 1000) / 10  # 假设最大光照1000
            self.device_data["Light"] = light
        
        # 更新气体浓度显示
        if "MQ2" in properties:
            gas = properties["MQ2"]
            self.gas_var.set(str(gas))
            self.gas_bar["value"] = min(gas, 3000) / 30  # 假设最大浓度3000
            self.device_data["MQ2"] = gas
        
        # 更新HW开关状态
        if "HW" in properties:
            hw = properties["HW"]
            self.hw_state_var.set("开启" if hw == 1 else "关闭")
            self.device_data["HW"] = hw
        
        # 更新电机状态
        if "Motor" in properties:
            motor = properties["Motor"]
            motor_text = "运行" if motor == "MOTOR_START" else "停止"
            self.motor_state_var.set(motor_text)
            self.device_data["Motor"] = motor
    
    # MQTT回调函数
    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.connected = True
            self.status_var.set("状态: 已连接")
            self.status_light.itemconfig("light", fill="green")
            self.device_status_var.set("在线")
            self.log_message("成功连接到MQTT服务器")
            
            # 订阅主题
            topic = self.sub_topic_var.get()
            client.subscribe(topic)
            self.log_message(f"已订阅主题: {topic}")
        else:
            self.status_var.set(f"状态: 连接失败 (代码: {rc})")
            self.status_light.itemconfig("light", fill="red")
            self.log_message(f"连接失败，返回代码: {rc}")
            self.log_message("错误代码含义: "
                            "1=协议错误, 2=客户端ID无效, 3=服务器不可用, "
                            "4=用户名/密码错误, 5=未授权")
    
    def on_disconnect(self, client, userdata, rc):
        if rc != 0:
            self.status_var.set(f"状态: 意外断开 (代码: {rc})")
            self.status_light.itemconfig("light", fill="red")
            self.device_status_var.set("离线")
            self.log_message(f"意外断开连接，返回代码: {rc}")
            self.connected = False
            
            # 尝试重新连接
            self.log_message("尝试重新连接...")
            try:
                client.reconnect()
            except Exception as e:
                self.log_message(f"重新连接失败: {str(e)}")
    
    def on_message(self, client, userdata, msg):
        try:
            payload = msg.payload.decode()
            self.log_message(f"收到消息 [{msg.topic}]:")
            
            # 尝试解析JSON格式
            try:
                data = json.loads(payload)
                print(data)
                pretty_payload = json.dumps(data, indent=2, ensure_ascii=False)
                self.log_message(pretty_payload)
                
                # 提取设备属性
                properties = {}
                
                # 格式1: 直接包含属性
                if "DHT11_T" in data:
                    properties = data
                # 格式2: 包含在"message"对象中的"data"字段
                elif "message" in data and "data" in data["message"]:
                    properties = data["message"]["data"]
                # 格式3: 包含在"content"字段中
                elif "content" in data:
                    try:
                        content = json.loads(data["content"])
                        if "message" in content and "data" in content["message"]:
                            properties = content["message"]["data"]
                    except:
                        pass
                # 格式4: 包含在"services"数组中
                elif "services" in data:
                    for service in data["services"]:
                        if service.get("service_id") == "hi3861" and "properties" in service:
                            properties = service["properties"]
                            break
                
                if properties:
                    self.log_message(f"提取到属性数据: {properties}")
                    # 在主线程中更新UI
                    self.root.after(0, self.update_device_display, properties)
                else:
                    self.log_message("未找到属性数据")
            
            except json.JSONDecodeError:
                self.log_message("无法解析JSON数据")
                self.log_message(payload)
            except Exception as e:
                self.log_message(f"解析消息时出错: {str(e)}")
        
        except Exception as e:
            self.log_message(f"处理消息时出错: {str(e)}")
    
    def on_closing(self):
        """关闭窗口时的清理操作"""
        self.log_message("正在关闭应用程序...")
        self.disconnect_mqtt()
        self.stop_shadow_monitor()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = HuaweiCloudTester(root)
    root.mainloop()