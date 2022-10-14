import os
import json
import requests

from dotenv import load_dotenv
from pymongo import MongoClient
from paho.mqtt import client as MQTT_Client


# Load ENV
load_dotenv()


# Setup Mongodb info
mongodb = MongoClient(
    f'{os.environ.get("SERVER_PROTOCOL")}://{os.environ.get("MONGO_USER")}:{os.environ.get("MONGO_PASSWORD")}@{os.environ.get("SERVER")}')[os.environ.get("DATABASE")]
inventory_data = mongodb['Inventory_Data']
code_data = mongodb['Item_Map']

client = MQTT_Client.Client()

@client.connect_callback()
def on_connect(client, userdata, flags_dict, reason):
    print(f"========== {'Start Connect':^15s} ==========")
    client.subscribe('#')

@client.disconnect_callback()
def on_disconnect(client, userdata, result_code):
    print(f"========== {'End Connect':^15s} ==========")

# Set the receive message action
@client.message_callback()
def on_message(self, userdata, msg):
    if(msg.topic == 'esp32/test'):
        print(msg.payload)
    elif(msg.topic == 'try/test'):
        print(msg.topic)
        print(msg.payload)
        data = json.loads(msg.payload.decode('utf-8'))
        condition = data.pop('add_type')
        code_filter = code_data.find({'code':{
            "$in": list(data.keys())
        }})
        cabinet_infoo = dict()
        for info_data in code_filter:
            if info_data['cabinet'] not in cabinet_infoo.keys():
                cabinet_info = inventory_data.find_one({"cabinet": info_data['cabinet']})
                cabinet_infoo.update({info_data['cabinet']:cabinet_info})
            increase_nb = data.get(info_data['code'])
            if condition == 1:
                cabinet_infoo[info_data['cabinet']]['num'][info_data['name']] += increase_nb
            else:
                cabinet_infoo[info_data['cabinet']]['num'][info_data['name']] -= increase_nb
        for k, v in cabinet_infoo.items():
            inventory_data.update_one({'cabinet':k}, {'$set':v})
    elif(msg.topic == 'light'):
        headers = {
                'Content-Type': 'application/json',
                'Authorization': 'key=' + os.environ.get('SERVER_TOKEN'),
            }

        body = {
                'notification': {'title': '警告！！',
                                    'body': '耗材櫃門並未緊閉，請派相關人員前往檢查！'
                                    },
                'to':
                    os.environ.get('DEVICE_TOKEN'),
                'priority': 'high',
                }
        response = requests.post("https://fcm.googleapis.com/fcm/send",headers = headers, data=json.dumps(body))
        print(response.status_code)
    else:
        print(msg.topic)
        print(msg.payload.decode('utf-8'))

# Set connect info
client.connect("192.168.1.97", 1883, 60)


# Start connect
client.loop_forever()