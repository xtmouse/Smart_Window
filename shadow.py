import requests
import datetime
import time

# 华为云IAM配置信息（保持与您原有代码一致）
IAM_CONFIG = {
    "MAIN_USER": "hid_ajrcxqbs-41_riz",
    "endpoint": "https://iam.cn-north-4.myhuaweicloud.com:443",
    "username": "xtgo",
    "password": "xt12345678",
    "domain": "cn-north-4",
    "project_name": "e35868f675bf4c1fa07205813fdff6bc"
}

# 新增IoT服务配置
IOT_CONFIG = {
    "region": "cn-north-4",  # 与IAM区域一致
    "service_endpoint": f"https://9e87ba4f3e.st1.iotda-app.{IAM_CONFIG['domain']}.myhuaweicloud.com:443"
}

def get_huawei_cloud_token():
    """获取华为云IAM访问令牌（您提供的原始函数）"""
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
                    "project": {"name": IAM_CONFIG["domain"]}
                }
            }
        }
        headers = {"Content-Type": "application/json"}
        response = requests.post(url, json=auth_data, headers=headers)
        
        if response.status_code == 201:
            token = response.headers.get('X-Subject-Token')
            if not token:
                return {"success": False, "error": "响应中缺少X-Subject-Token头部"}
            
            try:
                token_data = response.json()
                expires_at = token_data['token']['expires_at']
                expire_time = datetime.datetime.strptime(expires_at, "%Y-%m-%dT%H:%M:%S.%fZ")
                formatted_expire = expire_time.strftime("%Y-%m-%d %H:%M:%S")
                
                return {
                    "success": True,
                    "token": token,
                    "expire_time": formatted_expire,
                    "expire_timestamp": expire_time
                }
            except (KeyError, ValueError) as e:
                return {"success": False, "error": f"解析令牌响应失败: {str(e)}"}
        else:
            return {
                "success": False,
                "status_code": response.status_code,
                "error": f"获取Token失败: {response.text}"
            }
    except Exception as e:
        return {"success": False, "error": f"请求过程中出错: {str(e)}"}

class HuaweiIoTDeviceMonitor:
    def __init__(self):
        self.token_info = None
        self.device_cache = {}
        self.last_update = {}
        
    def refresh_token(self):
        """刷新Token（使用您原有的获取Token函数）"""
        print("正在刷新华为云Token...")
        self.token_info = get_huawei_cloud_token()
        if self.token_info.get("success"):
            print(f"Token刷新成功! 有效期至: {self.token_info['expire_time']}")
            return True
        else:
            print(f"Token刷新失败: {self.token_info.get('error', '未知错误')}")
            return False
    
    def token_is_valid(self):
        """检查Token是否有效"""
        if not self.token_info or not self.token_info.get("success"):
            return False
            
        # 检查Token是否过期（提前5分钟刷新）
        expire_time = self.token_info.get("expire_timestamp")
        if not expire_time:
            return False
            
        return datetime.datetime.utcnow() < (expire_time - datetime.timedelta(minutes=5))
    
    def get_iot_devices(self):
        """获取项目中的所有IoT设备"""
        if not self.token_is_valid() and not self.refresh_token():
            return None
            
        url = f"{IOT_CONFIG['service_endpoint']}/v5/iot/{IAM_CONFIG['project_name']}/devices"
        headers = {
            "Content-Type": "application/json",
            "X-Auth-Token": self.token_info["token"]
        }
        
        try:
            response = requests.get(url, headers=headers)
            if response.status_code == 200:
                return response.json().get("devices", [])
            else:
                print(f"获取设备列表失败: {response.status_code} - {response.text}")
                return None
        except Exception as e:
            print(f"请求设备列表时出错: {str(e)}")
            return None
    
    def get_device_properties(self, device_id):
        """获取指定设备的属性（设备影子）"""
        if not self.token_is_valid() and not self.refresh_token():
            return None
            
        url = f"{IOT_CONFIG['service_endpoint']}/v5/iot/{IAM_CONFIG['project_name']}/devices/{device_id}/shadow"
        headers = {
            "Content-Type": "application/json",
            "X-Auth-Token": self.token_info["token"]
        }
        
        try:
            response = requests.get(url, headers=headers)
            if response.status_code == 200:
                return response.json()
            elif response.status_code == 404:
                print(f"设备不存在: {device_id}")
            else:
                print(f"获取设备属性失败: {response.status_code} - {response.text}")
            return None
        except Exception as e:
            print(f"请求设备属性时出错: {str(e)}")
            return None
    
    def monitor_device(self, device_id, interval=30, max_updates=None):
        """实时监控设备属性变化"""
        update_count = 0
        last_properties = None
        
        print(f"开始监控设备: {device_id}")
        print("按Ctrl+C停止监控...")
        
        try:
            while max_updates is None or update_count < max_updates:
                # 刷新Token如果必要
                if not self.token_is_valid() and not self.refresh_token():
                    print("Token无效且刷新失败，等待重试...")
                    time.sleep(60)
                    continue
                
                # 获取设备属性
                device_data = self.get_device_properties(device_id)
                
                if device_data:
                    shadow = device_data.get("shadow", [{}])
                    if shadow:
                        # 提取上报属性
                        reported = shadow[0].get("reported", {})
                        
                        # 检测变化
                        changed = False
                        if last_properties is None:
                            changed = True
                        else:
                            for key, value in reported.items():
                                if key not in last_properties or last_properties[key] != value:
                                    changed = True
                                    break
                        
                        # 如果有变化或首次获取
                        if changed:
                            update_count += 1
                            timestamp = shadow[0].get("last_updated_time", "未知时间")
                            
                            print("\n" + "=" * 60)
                            print(f"设备ID: {device_id}")
                            print(f"更新时间: {timestamp}")
                            print(f"属性变化 (#{update_count}):")
                            
                            # 打印所有属性
                            for key, value in reported.items():
                                # 标记变化的属性
                                change_indicator = ""
                                if last_properties and key in last_properties and last_properties[key] != value:
                                    change_indicator = f" (变化: {last_properties.get(key)} → {value})"
                                print(f"  {key}: {value}{change_indicator}")
                            
                            print("=" * 60)
                            
                            # 更新最后已知属性
                            last_properties = reported.copy()
                
                # 等待下一次查询
                time.sleep(interval)
                
        except KeyboardInterrupt:
            print("\n监控已停止")
    
    def interactive_device_selection(self):
        """交互式选择设备并监控"""
        # 初始化Token
        if not self.token_is_valid() and not self.refresh_token():
            print("无法获取有效Token，请检查配置")
            return
            
        # 获取设备列表
        print("正在获取设备列表...")
        devices = self.get_iot_devices()
        if not devices:
            print("未找到设备，请检查项目配置")
            return
            
        # 显示设备列表
        print("\n您的项目中的IoT设备:")
        for idx, device in enumerate(devices, 1):
            device_name = device.get("device_name", "未命名设备")
            device_id = device.get("device_id", "未知ID")
            status = "在线" if device.get("status") == "ONLINE" else "离线"
            print(f"{idx}. {device_name} ({device_id}) - 状态: {status}")
        
        # 选择设备
        try:
            choice = int(input("\n请选择要监控的设备编号: ")) - 1
            if 0 <= choice < len(devices):
                device_id = devices[choice]["device_id"]
                interval = int(input("请输入监控间隔(秒，默认30): ") or 30)
                self.monitor_device(device_id, interval)
            else:
                print("选择无效!")
        except ValueError:
            print("请输入有效的数字!")

# 主程序入口
if __name__ == "__main__":
    monitor = HuaweiIoTDeviceMonitor()
    
    # 选项1: 直接指定设备ID监控
    # device_id = "your_device_id_here"
    # monitor.monitor_device(device_id)
    
    # 选项2: 交互式选择设备
    monitor.interactive_device_selection()