import socket
import asyncio
import json
import re
import torch
import websockets
import subprocess
import threading
import time
import queue
import os
import shutil
import numpy as np
import requests
import datetime
import paho.mqtt.client as mqtt
from collections import deque
from transformers import AutoModelForCausalLM, AutoTokenizer

# ===================== 配置区域 =====================
MODEL_PATH = "E:\\Qwen\\models\\qwen\\Qwen1___5-1___8B"
FUNASR_WS_URL = "ws://127.0.0.1:10096/"
UDP_IP = "0.0.0.0"
UDP_PORT = 8888
BUFFER_SIZE = 4096
SILENCE_DURATION = 1.0  # 1秒静默视为语音结束
MAX_RECORDING_DURATION = 5.0  # 最大录音时长（秒）
ENERGY_THRESHOLD = 40  # 语音能量阈值（根据环境调整）
SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2
USE_AI_MODEL = True
USE_VOICE_CONTROL = True
AUTO_CONTROL_PAUSE_DURATION = 1800  # 语音控制后暂停自动控制的时间（秒）30分钟

# 新增UDP控制目标配置
CONTROL_IP = "192.168.137.25"  # 开发板IP
CONTROL_PORT = 5566             # 开发板UDP端口

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
    "subscribe_topic": "$oc/devices/6863a00ad582f2001836ef74_dev1/sys/#",  # 使用通配符订阅所有系统消息
    "publish_topic": "$oc/devices/6863a00ad582f2001836ef74_dev1/sys/properties/report"
}

# 目标设备ID
TARGET_DEVICE_ID = "6863a00ad582f2001836ef74_dev1"

# ===================== 全局状态 =====================
class GlobalState:
    def __init__(self):
        self.auto_control_enabled = True
        self.last_voice_time = 0
        self.current_motor_state = 0  # 0:停止 1:顺时针(开窗) 2:逆时针(关窗)
        self.target_openness = 0      # 目标开窗比例(0-100)
        self.current_openness = 0     # 当前开窗比例(0-100)
        self.target_curtain = 0       # 目标窗帘比例(0-100)
        self.current_curtain = 0      # 当前窗帘比例(0-100)
        self.wake_word_detected = False
        self.sensor_data = {
            'temp': 0, 'humidity': 0, 'hw': 0, 
            'rain': 0, 'light': 0, 'smoke': 0
        }
        self.last_decision_data = None
        self.command_control_active = False  # 新增：命令控制激活标志
        self.command_control_end_time = 0    # 新增：命令控制结束时间

# ===================== 华为云监控系统 =====================
class HuaweiCloudMonitor:
    def __init__(self, global_state):
        self.global_state = global_state
        self.running = True
        self.shadow_monitor_thread = None
        self.mqtt_client = None
        self.last_shadow_data = None
        self.decision_system = WindowDecisionSystem()
        
        # 启动监控
        self.start_monitoring()
    
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
                    print("错误: 响应中缺少X-Subject-Token头部")
                    return None
                
                try:
                    token_data = response.json()
                    expires_at = token_data['token']['expires_at']
                    expire_time = datetime.datetime.strptime(expires_at, "%Y-%m-%dT%H:%M:%S.%fZ")
                    return token
                except (KeyError, ValueError) as e:
                    print(f"解析令牌响应失败: {str(e)}")
                    return None
            else:
                print(f"获取Token失败: {response.status_code} - {response.text}")
                return None
        except Exception as e:
            print(f"请求过程中出错: {str(e)}")
            return None
    
    def get_device_shadow(self):
        """获取指定设备的影子数据"""
        token = self.get_huawei_cloud_token()
        if not token:
            return None
            
        url = f"{IOT_CONFIG['service_endpoint']}/v5/iot/{IAM_CONFIG['project_id']}/devices/{TARGET_DEVICE_ID}/shadow"
        headers = {
            "Content-Type": "application/json",
            "X-Auth-Token": token
        }
        
        try:
            response = requests.get(url, headers=headers)
            if response.status_code == 200:
                return response.json()
            else:
                print(f"获取设备影子失败: {response.status_code} - {response.text}")
                return None
        except Exception as e:
            print(f"请求设备影子时出错: {str(e)}")
            return None
    
    def monitor_device_shadow(self):
        """实时监控设备影子数据变化"""
        print("启动设备影子监控...")
        
        while self.running:
            try:
                # 获取设备影子数据
                shadow_data = self.get_device_shadow()
                
                if shadow_data:
                    # 提取并打印报告的数据
                    shadows = shadow_data.get("shadow", [])
                    if shadows:
                        shadow = shadows[0]
                        reported = shadow.get("reported", {})
                        
                        # 打印报告的数据
                        if reported:
                            properties = reported.get("properties", {})
                            # 打印影子数据
                            print(f"影子数据: {json.dumps(properties, indent=2)}")
                            
                            # 更新传感器数据
                            self.update_sensor_data(properties)
                            
                            # 立即处理传感器数据
                            self.process_sensor_data()
                
                # 等待下一次查询
                time.sleep(20)  # 每15秒查询一次
                
            except Exception as e:
                print(f"影子监控异常: {str(e)}")
                time.sleep(30)  # 出错后等待30秒再试
    
    def connect_mqtt(self):
        """连接MQTT服务器"""
        print("连接MQTT服务器...")
        
        # 创建MQTT客户端
        self.mqtt_client = mqtt.Client(
            client_id=MQTT_CONFIG["client_id"], 
            protocol=mqtt.MQTTv311,
            callback_api_version=mqtt.CallbackAPIVersion.VERSION1
        )
        
        # 设置认证信息
        self.mqtt_client.username_pw_set(MQTT_CONFIG["username"], MQTT_CONFIG["password"])
        
        # 设置回调函数
        self.mqtt_client.on_connect = self.on_connect
        self.mqtt_client.on_message = self.on_message
        self.mqtt_client.on_disconnect = self.on_disconnect
        
        try:
            # 连接到MQTT服务器
            self.mqtt_client.connect(MQTT_CONFIG["server"], MQTT_CONFIG["port"], 60)
            
            # 启动网络循环
            self.mqtt_client.loop_start()
            
        except Exception as e:
            print(f"连接MQTT失败: {str(e)}")
            self.mqtt_client = None
    
    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print("成功连接到MQTT服务器")
            
            # 订阅主题
            topic = MQTT_CONFIG["subscribe_topic"]
            client.subscribe(topic)
            print(f"已订阅主题: {topic}")
        else:
            print(f"连接失败，返回代码: {rc}")
            print("错误代码含义: "
                  "1=协议错误, 2=客户端ID无效, 3=服务器不可用, "
                  "4=用户名/密码错误, 5=未授权")
    
    def on_disconnect(self, client, userdata, rc):
        if rc != 0:
            print(f"意外断开连接，返回代码: {rc}")
            # 尝试重新连接
            print("尝试重新连接...")
            try:
                client.reconnect()
            except Exception as e:
                print(f"重新连接失败: {str(e)}")
    
    def on_message(self, client, userdata, msg):
        try:
            payload = msg.payload.decode()
            
            # 尝试解析JSON格式
            try:
                data = json.loads(payload)
                print(f"收到MQTT消息 [{msg.topic}]: {json.dumps(data, indent=2)}")
                
                # 新增：处理命令消息
                if "commands" in msg.topic:
                    self.process_command_message(data)
                    return
                
                # 尝试提取设备属性
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
                # 新增格式5: 命令消息格式
                elif "paras" in data and "services" in data["paras"]:
                    for service in data["paras"]["services"]:
                        if service.get("service_id") == "hi3861" and "properties" in service:
                            properties = service["properties"]
                
                if properties:
                    print(f"提取到属性数据: {json.dumps(properties, indent=2)}")
                    # 更新传感器数据并立即处理
                    self.update_sensor_data(properties)
                    self.process_sensor_data()
                else:
                    print("未找到属性数据")
            
            except json.JSONDecodeError:
                print("无法解析JSON数据")
                print(payload)
            except Exception as e:
                print(f"解析消息时出错: {str(e)}")
        
        except Exception as e:
            print(f"处理消息时出错: {str(e)}")
    
    def process_command_message(self, data):
        """处理命令消息"""
        print("检测到命令消息")
        
        # 提取命令参数
        properties = {}
        if "paras" in data and "services" in data["paras"]:
            for service in data["paras"]["services"]:
                if service.get("service_id") == "hi3861" and "properties" in service:
                    properties.update(service["properties"])
        
        if not properties:
            print("命令消息中未找到有效属性")
            return
        
        print(f"命令消息属性: {json.dumps(properties, indent=2)}")
        
        # 更新目标比例
        updated = False
        if "Window_P" in properties:
            new_value = properties["Window_P"]
            if self.global_state.target_openness != new_value:
                self.global_state.target_openness = new_value
                updated = True
                print(f"命令设置目标开窗比例: {new_value}%")
        
        if "Curten_P" in properties:
            new_value = properties["Curten_P"]
            if self.global_state.target_curtain != new_value:
                self.global_state.target_curtain = new_value
                updated = True
                print(f"命令设置目标窗帘比例: {new_value}%")
        
        if updated:
            # 设置命令控制状态
            self.global_state.command_control_active = True
            self.global_state.command_control_end_time = time.time() + AUTO_CONTROL_PAUSE_DURATION
            print(f"命令控制成功，自动控制已暂停30分钟")
            
            # 立即发送控制指令
            self.send_immediate_control_commands()

    def send_immediate_control_commands(self):
        """立即发送控制指令"""
        # 计算步数转换参数
        WINDOW_STEPS_PER_PERCENT = 180 / 100  # 每1%开窗对应417步
        CURTAIN_STEPS_PER_PERCENT = 180 / 100    # 每1%窗帘对应1.8步
        
        # 计算窗户比例差
        window_diff = self.global_state.target_openness - self.global_state.current_openness
        # 计算需要移动的步数 (每1%开窗对应417步)
        window_steps = int(round(abs(window_diff) * WINDOW_STEPS_PER_PERCENT))
        
        # 计算窗帘比例差
        curtain_diff = self.global_state.target_curtain - self.global_state.current_curtain
        # 计算需要移动的步数 (每1%窗帘对应1.8步)
        curtain_steps = int((int(round(abs(curtain_diff) * CURTAIN_STEPS_PER_PERCENT))/10)*10)
        
        # 生成窗户控制指令
        window_cmd = ""
        if window_diff > 5:  # 避免微小调整
            window_cmd = f"m2 {window_steps}"  # 开窗
        elif window_diff < -5:
            window_cmd = f"m1 {window_steps}"  # 关窗
        
        # 生成窗帘控制指令
        curtain_cmd = ""
        if curtain_diff > 5:
            curtain_cmd = f"c2 {curtain_steps}"  # 开窗帘
        elif curtain_diff < -5:
            curtain_cmd = f"c1 {curtain_steps}"  # 关窗帘
        
        # 发送控制指令，窗帘优先
        if curtain_cmd:
            print(f"立即发送窗帘指令: {curtain_cmd}")
            send_control_command(curtain_cmd)
            time.sleep(5)  # 等待10秒
        
        if window_cmd:
            print(f"立即发送窗户指令: {window_cmd}")
            send_control_command(window_cmd)
            time.sleep(5)  # 等待10秒

    def update_sensor_data(self, properties):
        """更新传感器数据到全局状态"""
        # 华为云数据键到本地键的映射
        key_mapping = {
            "DHT11_T": "temp",
            "DHT11_H": "humidity",
            "HW": "hw",
            "Rain": "rain",
            "Light": "light",
            "MQ2": "smoke"
        }
        
        # 更新数据
        updated = False
        for huawei_key, local_key in key_mapping.items():
            if huawei_key in properties:
                new_value = properties[huawei_key]
                if self.global_state.sensor_data[local_key] != new_value:
                    self.global_state.sensor_data[local_key] = new_value
                    updated = True
        
        # 特殊处理新属性
        if "Motor" in properties:
            motor_value = properties["Motor"]
            if self.global_state.current_motor_state != motor_value:
                self.global_state.current_motor_state = motor_value
                updated = True
                print(f"电机状态更新: {motor_value}")
        
        if "Window_P" in properties:
            window_p = properties["Window_P"]
            if self.global_state.current_openness != window_p:
                self.global_state.current_openness = window_p
                updated = True
                print(f"窗户比例更新: {window_p}%")
        
        if "Curten_P" in properties:
            curtain_p = properties["Curten_P"]
            if self.global_state.current_curtain != curtain_p:
                self.global_state.current_curtain = curtain_p
                updated = True
                print(f"窗帘比例更新: {curtain_p}%")
        
        if updated:
            data = self.global_state
            print(f"状态更新: 电机={data.current_motor_state}, "
                  f"开窗={data.current_openness}%, 窗帘={data.current_curtain}% | "
                  f"温度={data.sensor_data['temp']}℃, 湿度={data.sensor_data['humidity']}%")
    
    def process_sensor_data(self):
        """处理传感器数据并做出决策"""
        # 检查是否启用自动控制
        if not self.global_state.auto_control_enabled:
            print("自动控制已暂停，跳过传感器决策")
            return
        
        # 获取当前传感器数据
        sensor_data = self.global_state.sensor_data.copy()
        
        # 检查数据是否与上次相同
        if sensor_data == self.global_state.last_decision_data:
            print("传感器数据未变化，跳过决策")
            return
        
        # 记录本次处理的数据
        self.global_state.last_decision_data = sensor_data.copy()
        
        # 打印传感器数据
        print(f"\n处理传感器数据: 温度={sensor_data['temp']}℃, 湿度={sensor_data['humidity']}%, "
              f"人体检测={'是' if sensor_data['hw'] else '否'}, 雨滴={sensor_data['rain']}%, "
              f"光照={sensor_data['light']:.1f}, 烟雾={sensor_data['smoke']:.1f}")
        
        # 做出决策 - 修复解包错误
        window_target, curtain_target, reason = self.decision_system.make_decision(sensor_data)
        
        # 更新目标开窗比例和目标窗帘比例
        self.global_state.target_openness = window_target
        self.global_state.target_curtain = curtain_target
        print(f"自动决策: {reason} | 目标开窗比例: {window_target}% | 目标窗帘比例: {curtain_target}%")
    
    def start_monitoring(self):
        """启动监控"""
        # 首先获取一次设备影子
        print("获取初始设备影子...")
        initial_shadow = self.get_device_shadow()
        if initial_shadow:
            # 提取并打印报告的数据
            shadows = initial_shadow.get("shadow", [])
            if shadows:
                shadow = shadows[0]
                reported = shadow.get("reported", {})
                if reported:
                    properties = reported.get("properties", {})
                    print(f"初始影子数据: {json.dumps(properties, indent=2)}")
                    self.update_sensor_data(properties)
                    # 立即处理初始数据
                    self.process_sensor_data()
        
        # 启动影子监控线程
        self.shadow_monitor_thread = threading.Thread(target=self.monitor_device_shadow, daemon=True)
        self.shadow_monitor_thread.start()
        
        # 连接MQTT
        self.connect_mqtt()
        
        print("\n华为云监控已启动，正在持续接收数据更新...")
    
    def stop_monitoring(self):
        """停止监控"""
        print("停止华为云监控...")
        self.running = False
        
        # 停止MQTT连接
        if self.mqtt_client:
            try:
                self.mqtt_client.loop_stop()
                self.mqtt_client.disconnect()
            except:
                pass
        
        # 等待影子监控线程结束
        if self.shadow_monitor_thread and self.shadow_monitor_thread.is_alive():
            self.shadow_monitor_thread.join(timeout=5)
        
        print("华为云监控已停止")

# ===================== 音频接收系统 =====================
class AudioReceiver:
    def __init__(self, global_state):
        self.global_state = global_state
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((UDP_IP, UDP_PORT))
        self.audio_buffer = deque()
        self.last_packet_time = time.time()
        self.recording_start_time = None
        self.active = True
        self.packet_count = 0
        self.total_bytes = 0
        self.pcm_queue = queue.Queue()
        
        # 检查FFmpeg是否可用
        self.ffmpeg_path = self.find_ffmpeg()
        if not self.ffmpeg_path:
            print("错误：未找到FFmpeg！请安装FFmpeg并将其添加到系统路径")
            return
        
        # 启动接收线程
        self.receiver_thread = threading.Thread(target=self.receive_audio)
        self.receiver_thread.daemon = True
        self.receiver_thread.start()
        
        # 启动语音活动检测线程
        self.vad_thread = threading.Thread(target=self.voice_activity_detector)
        self.vad_thread.daemon = True
        self.vad_thread.start()
    
    def find_ffmpeg(self):
        """查找FFmpeg可执行文件路径"""
        # 检查常见安装路径
        common_paths = [
            "ffmpeg",  # 首先检查PATH
            "E:\\ffmpeg-7.0.2-essentials_build\\bin\\ffmpeg.exe",
        ]
        
        for path in common_paths:
            if shutil.which(path):
                return shutil.which(path)
        
        return None
    
    def receive_audio(self):
        """持续接收UDP音频数据"""
        print(f"Listening for UDP packets on {UDP_IP}:{UDP_PORT}")
        print("Press Ctrl+C to stop...")
        print(f"配置: 静默检测={SILENCE_DURATION}秒, 最大录音={MAX_RECORDING_DURATION}秒, 能量阈值={ENERGY_THRESHOLD}")
        
        try:
            while self.active:
                data, addr = self.sock.recvfrom(BUFFER_SIZE)
                current_time = time.time()
                
                # 如果是第一个包，记录开始时间
                if self.recording_start_time is None:
                    self.recording_start_time = current_time
                    print("检测到语音开始")
                
                # 将数据添加到缓冲区
                self.audio_buffer.append(data)
                self.last_packet_time = current_time
                self.packet_count += 1
                self.total_bytes += len(data)
                
                # 实时显示统计信息
                if self.packet_count % 10 == 0:
                    duration = current_time - self.recording_start_time if self.recording_start_time else 0
                    print(f"\r接收中: {duration:.1f}s, {self.packet_count}包, {self.total_bytes}字节", end='', flush=True)
        
        except Exception as e:
            print(f"\n接收错误: {str(e)}")
    
    def voice_activity_detector(self):
        """语音活动检测线程"""
        while self.active:
            time.sleep(0.1)  # 每100ms检查一次
            current_time = time.time()
            
            # 如果没有开始录音，跳过
            if self.recording_start_time is None:
                continue
                
            # 计算当前录音时长
            recording_duration = current_time - self.recording_start_time
            # 检查是否超过最大录音时长
            if recording_duration > MAX_RECORDING_DURATION:
                print("\n达到最大录音时长，处理音频")
                self.process_audio_segment()
                continue
                
            # 检查静默时长
            silence_duration = current_time - self.last_packet_time
            # 如果有静默且超过阈值
            if silence_duration > SILENCE_DURATION and len(self.audio_buffer) > 0:
                print(f"\n检测到静默 ({silence_duration:.1f}秒)，处理音频")
                self.process_audio_segment()
    
    def process_audio_segment(self):
        """处理音频段"""
        if len(self.audio_buffer) == 0:
            self.recording_start_time = None
            return
            
        # 合并所有缓冲数据
        aac_data = b''.join(self.audio_buffer)
        self.audio_buffer.clear()
        
        # 计算能量（可选，用于调试）
        energy = self.calculate_energy(aac_data)
        print(f"处理音频段 ({len(aac_data)}字节, 能量={energy})")
        
        # 如果能量过低，视为噪音，跳过处理
        if energy < ENERGY_THRESHOLD:
            print("能量过低，跳过处理（可能是背景噪音）")
            self.recording_start_time = None
            return
        
        # 启动语音处理线程
        threading.Thread(
            target=self.process_audio_command, 
            args=(aac_data,),
            daemon=True
        ).start()
        
        # 重置录音状态
        self.recording_start_time = None
    
    def calculate_energy(self, audio_data):
        """计算音频能量（简单实现）"""
        try:
            # 使用FFmpeg解码为PCM
            pcm_data = self.decode_aac_to_pcm(audio_data)
            if not pcm_data:
                return 0
                
            # 转换为numpy数组
            audio_array = np.frombuffer(pcm_data, dtype=np.int16)
            
            # 计算RMS能量
            if len(audio_array) == 0:
                return 0
                
            rms = np.sqrt(np.mean(audio_array**2))
            return rms
            
        except Exception as e:
            print(f"能量计算错误: {str(e)}")
            return 0
    
    def process_audio_command(self, aac_data):
        """处理完整的音频段"""
        # 创建新的事件循环
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        
        try:
            # 直接解码AAC到PCM
            pcm_data = self.decode_aac_to_pcm(aac_data)
            if pcm_data is None:
                print("AAC解码失败")
                return
            
            # 初始化语音控制系统
            voice_system = VoiceControlSystem(self.global_state)
            
            # 处理语音指令 - 修复解包错误
            window_target, curtain_target, reason = loop.run_until_complete(
                voice_system.process_audio(pcm_data))
            
            if window_target is not None or curtain_target is not None:
                print(f"\n决策结果: 目标开窗比例 {window_target}% | 目标窗帘比例 {curtain_target}% | 原因: {reason}")
                
                # 更新目标比例
                if window_target is not None:
                    self.global_state.target_openness = window_target
                if curtain_target is not None:
                    self.global_state.target_curtain = curtain_target
                
                # 记录语音控制时间
                self.global_state.last_voice_time = time.time()
                
                # 语音控制后暂停自动控制
                self.global_state.auto_control_enabled = False
                print(f"语音控制成功，自动控制已暂停")
            else:
                print("语音指令处理失败")
        
        except Exception as e:
            print(f"语音处理错误: {str(e)}")
        finally:
            loop.close()
    
    def decode_aac_to_pcm(self, aac_data):
        """使用FFmpeg将AAC数据解码为PCM"""
        try:
            command = [
                self.ffmpeg_path,
                '-i', 'pipe:0',
                '-f', 's16le',
                '-acodec', 'pcm_s16le',
                '-ac', str(CHANNELS),
                '-ar', str(SAMPLE_RATE),
                '-y',
                'pipe:1'
            ]
            
            process = subprocess.Popen(
                command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            
            pcm_data, stderr = process.communicate(input=aac_data)
            
            if process.returncode != 0:
                error_msg = stderr.decode('utf-8', errors='ignore')[:200]  # 截取部分错误信息
                print(f"FFmpeg解码错误: {error_msg}")
                return None
                
            
            return pcm_data
        except Exception as e:
            print(f"AAC解码错误: {str(e)}")
            return None

# ===================== 语音控制系统 =====================
class VoiceControlSystem:
    def __init__(self, global_state):
        self.global_state = global_state
        self.decision_system = WindowDecisionSystem()
        self.websocket = None
        self.transcription = ""
    
    async def process_audio(self, pcm_data):
        """处理PCM音频数据并返回决策"""
        try:
            # 初始化WebSocket连接
            self.websocket = await websockets.connect(
                FUNASR_WS_URL,
                ping_interval=None,
                ping_timeout=None,
                close_timeout=10
            )
            
            # 发送开始消息
            await self.websocket.send(json.dumps({
                "chunk_size": [5, 10, 5],
                "wav_name": "realtime_audio",
                "is_speaking": True,
                "mode": "2pass"
            }))
            
            print("连接FUNASR成功，开始识别...")
            
            # 分块发送数据
            chunk_size = 4096
            total_bytes = len(pcm_data)
            bytes_sent = 0
            
            while bytes_sent < total_bytes:
                # 计算当前块大小
                current_chunk_size = min(chunk_size, total_bytes - bytes_sent)
                chunk = pcm_data[bytes_sent:bytes_sent + current_chunk_size]
                
                # 发送数据块
                await self.websocket.send(chunk)
                bytes_sent += current_chunk_size
                
                # 尝试获取中间结果
                try:
                    result = await asyncio.wait_for(self.websocket.recv(), timeout=0.1)
                    result_json = json.loads(result)
                    
                    # 更新转录文本
                    new_text = result_json.get("text", "")
                    if new_text and new_text != self.transcription:
                        self.transcription = new_text
                        print(f"\r识别中: {self.transcription}", end='', flush=True)
                    
                    # 检查是否结束
                    if result_json.get("is_final", False):
                        break
                except asyncio.TimeoutError:
                    # 没有新数据，继续发送
                    continue
            
            # 发送结束消息
            await self.websocket.send(json.dumps({"is_speaking": False}))
            
            # 获取最终结果
            result = await self.websocket.recv()
            result_json = json.loads(result)
            final_text = result_json.get("text", "")
            
            # 更新最终转录文本
            if final_text:
                self.transcription = final_text
                print(f"\n最终识别: {self.transcription}")
            
            # 检测唤醒词
            if not self.global_state.wake_word_detected:
                if self.check_wake_word(self.transcription):
                    print("检测到唤醒词，等待指令...")
                    self.global_state.wake_word_detected = True
                    return None, None, "唤醒成功"
                else:
                    print("未检测到唤醒词，忽略指令")
                    return None, None, "未唤醒"
            
            # 处理语音指令 - 返回三个值
            window_target, curtain_target = self.parse_voice_command(self.transcription)
            if window_target is not None or curtain_target is not None:
                # 检查恢复指令
                if self.check_recovery_command(self.transcription):
                    self.global_state.auto_control_enabled = True
                    self.global_state.wake_word_detected = False
                    print("检测到恢复指令，自动控制已启用")
                    return None, None, "恢复自动控制"
                
                return window_target, curtain_target, f"语音指令: {self.transcription}"
            
            return None, None, "未识别到有效指令"
        
        except Exception as e:
            print(f"语音识别错误: {str(e)}")
            return None, None, f"识别失败: {str(e)}"

        
        finally:
            # 关闭连接
            if self.websocket:
                try:
                    await self.websocket.close()
                except:
                    pass
                self.websocket = None
    
    def check_wake_word(self, text):
        """检测唤醒词"""
        wake_words = ["天", "填", "甜", "田", "添"]
        return any(word in text for word in wake_words)
    
    def check_recovery_command(self, text):
        """检测恢复指令"""
        recovery_words = ["恢复", "恢复自动", "开启自动", "自动控制"]
        return any(word in text for word in recovery_words)
    
    def parse_voice_command(self, command):
        """解析语音指令，返回目标开窗比例和窗帘比例"""
        # 初始化默认值
        window_target = None
        curtain_target = None
        
        # 关闭指令
        if any(word in command for word in ["关", "关闭", "合上", "关上", "全关"]):
            # 判断对象
            if "帘" in command:
                curtain_target = 0
            elif "窗" in command:
                window_target = 0
            else:  # 未指定对象，同时关闭窗和窗帘
                window_target = 0
                curtain_target = 0
        
        # 全开指令
        elif any(word in command for word in ["开", "打开", "全开", "开启", "掰开"]):
            if "帘" in command:
                curtain_target = 100
            elif "窗" in command:
                window_target = 100
            else:  # 未指定对象，同时开启窗和窗帘
                window_target = 100
                curtain_target = 100
        
        # 停止指令
        elif any(word in command for word in ["停", "停止", "停下", "别动"]):
            # 保持当前比例
            window_target = self.global_state.current_openness
            curtain_target = self.global_state.current_curtain
        
        # 百分比指令 (例如 "开窗30%" 或 "窗帘50%")
        else:
            # 窗户指令
            window_match = re.search(r'窗[帘]?[开]?(\d{1,3})%?', command)
            if window_match:
                percent = int(window_match.group(1))
                window_target = max(0, min(percent, 100))
            
            # 窗帘指令
            curtain_match = re.search(r'帘[开]?(\d{1,3})%?', command)
            if curtain_match:
                percent = int(curtain_match.group(1))
                curtain_target = max(0, min(percent, 100))
        
        return window_target, curtain_target

# ===================== 窗户决策系统 =====================
class WindowDecisionSystem:
    def __init__(self):
        self.model = None
        self.tokenizer = None
        
        if USE_AI_MODEL:
            self.load_model()
    
    def load_model(self):
        try:
            print(f"加载模型: {MODEL_PATH}")
            self.model = AutoModelForCausalLM.from_pretrained(
                MODEL_PATH,
                device_map="auto",
                torch_dtype=torch.float16 if torch.cuda.is_available() else torch.float32
            )
            self.tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH)
            self.model.eval()
            print(f"模型加载完成 | 设备: {self.model.device}")
        except Exception as e:
            print(f"模型加载失败: {e}")
            self.model = None
    
    def make_decision(self, sensor_data):
        """根据传感器数据做出决策，返回目标开窗比例和窗帘比例"""
        # 提取传感器值
        temp = sensor_data['temp']
        humidity = sensor_data['humidity']
        hw = sensor_data['hw']
        rain = sensor_data['rain']
        light = sensor_data['light']
        smoke = sensor_data['smoke']
        
        # 1. 如果检测到人体，停止调整
        if hw == 1:
            return 0, 0, "检测到人体，停止调整"
        
        # 2. 如果检测到烟雾值高，开窗100%
        if smoke > 1000:
            return 100, 0, f"烟雾值高({smoke})，开窗通风"
        
        # 3. 如果检测到下雨，关窗0%
        if rain > 50:
            return 0, 0, f"检测到下雨({rain}%)，关窗"
        
        # 4. 使用AI或规则决策
        if USE_AI_MODEL and self.model is not None:
            try:
                window_target, curtain_target = self.ai_decision(temp, humidity, light, smoke)
                return window_target, curtain_target, "AI决策"
            except Exception as e:
                print(f"AI决策失败: {e}, 使用规则系统")
                window_target, curtain_target, reason = self.rule_based_decision(temp, humidity, light, smoke)
                return window_target, curtain_target, f"规则决策: {reason}"
        
        # 5. 使用基于规则的决策系统
        window_target, curtain_target, reason = self.rule_based_decision(temp, humidity, light, smoke)
        return window_target, curtain_target, f"规则决策: {reason}"
    
    def ai_decision(self, temp, humidity, light, smoke):
        """使用AI模型做出决策，返回目标开窗比例(0-100)"""
        prompt = self.build_prompt(temp, humidity, light, smoke)
        
        # 编码输入
        inputs = self.tokenizer(prompt, return_tensors="pt").to(self.model.device)
        
        # 生成响应
        with torch.no_grad():
            outputs = self.model.generate(
                **inputs,
                max_new_tokens=50,
                do_sample=False,
                pad_token_id=self.tokenizer.eos_token_id
            )
        
        # 解码输出
        full_response = self.tokenizer.decode(outputs[0], skip_special_tokens=True)
        
        # 提取决策部分
        response = full_response[len(prompt):].strip()
        
        # 尝试提取开窗和窗帘比例
        window_match = re.search(r'开窗[:：]?\s*(\d{1,3})%?', response)
        curtain_match = re.search(r'窗帘[:：]?\s*(\d{1,3})%?', response)
        
        window_percent = int(window_match.group(1)) if window_match else 50
        curtain_percent = int(curtain_match.group(1)) if curtain_match else 50
        
        return (
            max(0, min(window_percent, 100)),
            max(0, min(curtain_percent, 100)))
    
    def build_prompt(self, temp, humidity, light, smoke):
        """构建AI提示，包含窗帘控制"""
        return f"""
[智能窗户控制系统]
请根据环境数据决定目标开窗比例(0-100%)和窗帘比例(0-100%)。遵循以下规则：

[环境数据]
温度={temp}℃ | 湿度={humidity}% | 光照={light} | 烟雾={smoke}

[决策规则]
1. 安全优先：
   - 烟雾值高(>1000)时开窗100%，窗帘不变
   - 下雨时关窗0%，窗帘不变
   
2. 舒适度优化：
   - 温度舒适范围(18-25℃)，过高开窗通风，过低关窗保温
   - 光照过强时降低窗帘比例，过暗时提高窗帘比例

3. 输出格式：
   开窗:[比例]%
   窗帘:[比例]%
"""
    
    def rule_based_decision(self, temp, humidity, light, smoke):
        """基于环境评分的决策系统，返回目标开窗比例(0-100)"""
        # 1. 计算温度评分 (权重 30%)
        if 18 <= temp <= 25:
            temp_score = 1.0
            temp_reason = "温度适宜"
        elif (15 <= temp < 18) or (25 < temp <= 28):
            temp_score = 0.7
            temp_reason = "温度稍低" if temp < 18 else "温度稍高"
        elif (10 <= temp < 15) or (28 < temp <= 32):
            temp_score = 0.4
            temp_reason = "温度较低" if temp < 15 else "温度较高"
        else:
            temp_score = 0.1
            temp_reason = "温度过低" if temp < 10 else "温度过高"
        
        # 2. 计算湿度评分 (权重 20%)
        if 40 <= humidity <= 60:
            humid_score = 1.0
            humid_reason = "湿度适宜"
        elif (30 <= humidity < 40) or (60 < humidity <= 70):
            humid_score = 0.7
            humid_reason = "湿度稍低" if humidity < 40 else "湿度稍高"
        elif (20 <= humidity < 30) or (70 < humidity <= 80):
            humid_score = 0.4
            humid_reason = "湿度较低" if humidity < 30 else "湿度较高"
        else:
            humid_score = 0.1
            humid_reason = "湿度过低" if humidity < 20 else "湿度过高"
        
        # 3. 计算光照评分 (权重 20%)
        if 40 <= light <= 80:
            light_score = 1.0
            light_reason = "光照适宜"
        elif (20 <= light < 40) or (80 < light <= 100):
            light_score = 0.7
            light_reason = "光照稍暗" if light < 40 else "光照稍强"
        else:
            light_score = 0.4
            light_reason = "光照过暗" if light < 20 else "光照过强"
        
        # 4. 计算烟雾评分 (权重 30%)
        if smoke < 500:
            smoke_score = 1.0
            smoke_reason = "空气质量良好"
        elif 500 <= smoke < 1000:
            smoke_score = 0.6
            smoke_reason = "空气质量中等"
        elif 1000 <= smoke < 2000:
            smoke_score = 0.3
            smoke_reason = "空气质量差"
        else:
            smoke_score = 0.0
            smoke_reason = "空气质量危险"
        
        # 5. 计算综合环境评分
        environment_score = (
            (temp_score * 0.3) + 
            (humid_score * 0.2) + 
            (light_score * 0.2) + 
            (smoke_score * 0.3))
        
        # 6. 根据评分计算目标开窗比例
        window_target = int(environment_score * 100)
        reason = f"环境评分={environment_score:.2f} | 详细: {temp_reason}, {humid_reason}, {light_reason}, {smoke_reason}"

        # 新增窗帘决策逻辑
        # 窗帘决策基于光照强度
        if light < 20:
            curtain_target = 100  # 光照弱，全开窗帘
            curtain_reason = "光照弱，全开窗帘"
        elif light < 40:
            curtain_target = 75
            curtain_reason = "光照较弱，开窗帘75%"
        elif light < 60:
            curtain_target = 50
            curtain_reason = "光照适中，开窗帘50%"
        elif light < 80:
            curtain_target = 25
            curtain_reason = "光照较强，开窗帘25%"
        else:
            curtain_target = 0  # 光照强，全关窗帘
            curtain_reason = "光照强，关闭窗帘"
        
        reason = f"{reason} | {curtain_reason}"
        return window_target, curtain_target, reason


# ===================== UDP控制发送函数 =====================
def send_control_command(command):
    """通过UDP发送控制指令到目标设备"""
    try:
        # 创建UDP套接字
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # 发送指令
        sock.sendto(command.encode(), (CONTROL_IP, CONTROL_PORT))
        print(f"已发送控制指令: {command}")
        # 关闭套接字
        sock.close()
        return True
    except Exception as e:
        print(f"发送控制指令失败: {str(e)}")
        return False


# ===================== 主程序入口 =====================
if __name__ == "__main__":
    # 初始化全局状态
    global_state = GlobalState()
    
    # 启动华为云监控
    huawei_monitor = HuaweiCloudMonitor(global_state)
    
    # 启动音频接收器
    receiver = AudioReceiver(global_state)
    
    # 保持主线程运行s
    try:
        last_print_time = 0
        last_control_time = 0
        control_interval = 15  # 控制指令发送间隔(秒)
        
        # 定义步数转换参数
        WINDOW_STEPS_PER_PERCENT = 180 / 100  # 每1%开窗对应417步
        CURTAIN_STEPS_PER_PERCENT = 180 / 100    # 每1%窗帘对应1.8步
        
        while True:
            current_time = time.time()
            
            # 检查命令控制状态
            if global_state.command_control_active:
                if current_time > global_state.command_control_end_time:
                    global_state.command_control_active = False
                    global_state.auto_control_enabled = True
                    print("\n命令控制结束，自动控制已恢复")
                else:
                    # 命令控制期间跳过所有控制指令发送
                    time_left = global_state.command_control_end_time - current_time
                    status = (f"命令控制中({int(time_left/60)}分{int(time_left%60)}秒): "
                              f"窗[{global_state.current_openness}%] "
                              f"帘[{global_state.current_curtain}%]")
                    print(f"\r{status}", end='', flush=True)
                    time.sleep(1)
                    continue  # 跳过后续控制逻辑
            
            # 检查自动控制暂停状态
            if not global_state.auto_control_enabled:
                # 检查是否应该恢复自动控制
                if current_time - global_state.last_voice_time > AUTO_CONTROL_PAUSE_DURATION:
                    global_state.auto_control_enabled = True
                    print("\n自动控制已恢复")
            
            # 定期发送控制指令
            if current_time - last_control_time > control_interval and global_state.auto_control_enabled:
                last_control_time = current_time
                
                # 计算窗户比例差
                window_diff = global_state.target_openness - global_state.current_openness
                # 计算需要移动的步数 (每1%开窗对应417步)
                window_steps = int(round(abs(window_diff) * WINDOW_STEPS_PER_PERCENT))  # 四舍五入取整
                
                # 计算窗帘比例差
                curtain_diff = global_state.target_curtain - global_state.current_curtain
                # 计算需要移动的步数 (每1%窗帘对应1.8步)
                curtain_steps = int((int(round(abs(curtain_diff) * CURTAIN_STEPS_PER_PERCENT))/10)*10)
                
                # 生成窗户控制指令
                window_cmd = ""
                if window_diff > 5:  # 避免微小调整
                    window_cmd = f"m2 {window_steps}"  # 开窗
                elif window_diff < -5:
                    window_cmd = f"m1 {window_steps}"  # 关窗
                
                # 生成窗帘控制指令
                curtain_cmd = ""
                if curtain_diff > 5:
                    curtain_cmd = f"c2 {curtain_steps}"  # 开窗帘
                elif curtain_diff < -5:
                    curtain_cmd = f"c1 {curtain_steps}"  # 关窗帘
                
                # 发送控制指令
                if curtain_cmd:
                    send_control_command(curtain_cmd)
                    time.sleep(5)
                if window_cmd:
                    send_control_command(window_cmd)
                    time.sleep(10)
                
            # 打印当前状态
            status = (f"状态: 窗[{global_state.current_openness}%→{global_state.target_openness}%] "
                      f"帘[{global_state.current_curtain}%→{global_state.target_curtain}%] "
                      f"自动={'开' if global_state.auto_control_enabled else '关'}")
            if global_state.wake_word_detected:
                status += " | 等待指令"
            print(f"\r{status}", end='', flush=True)
            
            time.sleep(1)
            
    except KeyboardInterrupt:
        # 停止所有组件
        receiver.active = False
        huawei_monitor.stop_monitoring()
        print("\n程序已停止")