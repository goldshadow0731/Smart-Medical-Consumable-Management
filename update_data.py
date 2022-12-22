import hashlib
import os
import json
import random
import requests

import cv2
from dotenv import load_dotenv
from paho.mqtt import client as MQTT_Client
from pymongo import MongoClient

from epaper import GenerateEPaperImage
from snowflakes import UIDGenerator


# Load ENV
load_dotenv(f"{os.path.dirname(os.path.abspath(__file__))}/.env")


# Setup Mongodb info
mongodb = MongoClient(
    "{protocol}://{user}:{password}@{server}".format(
        protocol=os.environ.get("SERVER_PROTOCOL"),
        user=os.environ.get("MONGO_USER"),
        password=os.environ.get("MONGO_PASSWORD"),
        server=os.environ.get("SERVER")
    )
)[os.environ.get("DATABASE")]
inventory_data = mongodb['Inventory_Data']
code_data = mongodb['Item_Map']
image_data = mongodb['Image_Data']

client = MQTT_Client.Client()


@client.connect_callback()
def on_connect(client, userdata, flags_dict, reason):
    print(f"========== {'Start Connect':^15s} ==========")
    client.subscribe('#')


@client.disconnect_callback()
def on_disconnect(client, userdata, result_code):
    print(f"========== {'End Connect':^15s} ==========")


@client.message_callback()  # Set the receive message action
def on_message(self, userdata, msg):
    print(f"{msg.topic} - {msg.payload}")
    if msg.topic.startswith("draw"):
        topic_list = msg.topic.split("/")
        action = topic_list[0]
        cabinet = topic_list[1]
        channel = topic_list[2]
        topic = f"draw/{cabinet}/payload"
        qos_level = 1
        msg_data = msg.payload
        if channel == "init":
            tmp_1 = msg_data[0] + 1
            buffer_size = int(msg_data[1:tmp_1].decode("utf-8"))
            tmp_2 = msg_data[tmp_1] + tmp_1 + 1
            epaper_width = int(msg_data[tmp_1 + 1:tmp_2].decode("utf-8"))
            tmp_3 = msg_data[tmp_2] + tmp_2 + 1
            epaper_height = int(msg_data[tmp_2 + 1:tmp_3].decode("utf-8"))

            image_id = UIDGenerator(ord(cabinet)).get_id()
            g_image = GenerateEPaperImage(epaper_width, epaper_height).gen_image(
                mongodb['Inventory_Data'].find_one({"cabinet": "A"})
            )
            c_data = g_image.crypto_data
            # cv2.imwrite("photo.jpg", g_image.image)
            # cv2.imshow("Photo", g_image.image)
            # cv2.waitKey(0)
            # cv2.destroyAllWindows()

            payload_size = buffer_size - (
                len(list(filter(
                    lambda x: buffer_size >= x,
                    [0, 129, 16386, 2097155, 268435460]
                ))) + 1  # Fixed header
            ) - (
                2 + len(topic)  # Topic
            ) - (
                2 if qos_level > 0 else 0  # Message identifier
            ) - 32  # Costom header
            seq_num = random.randint(0x00000000, 0x007fffff)

            # with open("./image.json") as fp:
            #     data = json.load(fp)
            # data.update({
            #     cabinet: {
            #         "topic": topic,
            #         "seq": seq_num,
            #         "image_id": image_id,
            #         "crypto_data": list(c_data),
            #         "data_length": len(c_data),
            #         "payload_size": payload_size
            #     }
            # })
            # with open("./image.json", "w") as fp:
            #     json.dump(data, fp, ensure_ascii=False, indent=4)
            image_data.insert_one({
                "cabinet": cabinet,
                "seq": seq_num,
                "image_id": image_id,
                "crypto_data": list(c_data),
                "data_length": len(c_data),
                "payload_size": payload_size
            })

            send_data = seq_num.to_bytes(3, 'big')  # seq
            send_data += 0x0c.to_bytes(1, 'big')  # flags
            send_data += int(0).to_bytes(3, 'big')  # ack
            send_data += int(0).to_bytes(1, 'big')  # status
            send_data += image_id.to_bytes(8, 'big')  # Image ID
            send_data += int(0).to_bytes(16, 'big')  # MD5
            send_data += c_data[:payload_size]
            md5 = hashlib.md5()
            md5.update(send_data)
            send_data = send_data[:16] + md5.digest() + send_data[32:]
            client.publish(topic, send_data, qos=qos_level)
        elif channel == "status":
            # 7 bit: Done
            # 6 bit: Success
            # 5 bit: Transport error
            # 4 bit: MD5 check error
            # 3 bit: Error image ID
            # 2 bit: Decompress error
            # 1 bit: Error data
            # 0 bit: Unable to allocate required memory
            resp_seq = ((msg_data[4] * 256) +
                        msg_data[5]) * 256 + msg_data[6]
            # with open("./image.json") as fp:
            #     data = json.load(fp)
            data = image_data.find_one({
                "image_id": int.from_bytes(msg_data[8:16], 'big'),
                "cabinet": cabinet
            })
            c_data = bytes(data["crypto_data"])

            if msg_data[7] in [0x40, 0x10, 0x08, 0x04]:
                payload_size = data["payload_size"]
                bias_num = resp_seq - data["seq"]

                send_data = resp_seq.to_bytes(3, 'big')  # seq
                flags = 0x0b if (
                    bias_num + payload_size) >= data["data_length"] else 0x09
                send_data += flags.to_bytes(1, 'big')  # flags
                send_data += msg_data[0:3]  # ack
                send_data += int(0).to_bytes(1, 'big')  # status
                # Image ID
                send_data += data["image_id"].to_bytes(8, 'big')
                send_data += int(0).to_bytes(16, 'big')  # MD5
                send_data += c_data[bias_num:(bias_num + payload_size)]
                md5 = hashlib.md5()
                md5.update(send_data)
                send_data = send_data[:16] + md5.digest() + send_data[32:]
                client.publish(topic, send_data, qos=qos_level)
    elif msg.topic == 'try/test':
        data = json.loads(msg.payload.decode('utf-8'))
        condition = data.pop('add_type')
        code_filter = code_data.find({'code': {
            "$in": list(data.keys())
        }})
        cabinet_infoo = dict()
        for info_data in code_filter:
            if info_data['cabinet'] not in cabinet_infoo.keys():
                cabinet_info = inventory_data.find_one(
                    {"cabinet": info_data['cabinet']})
                cabinet_infoo.update({info_data['cabinet']: cabinet_info})
            increase_nb = data.get(info_data['code'])
            if condition == 1:
                cabinet_infoo[info_data['cabinet']
                              ]['num'][info_data['name']] += increase_nb
            else:
                cabinet_infoo[info_data['cabinet']
                              ]['num'][info_data['name']] -= increase_nb
        for k, v in cabinet_infoo.items():
            inventory_data.update_one({'cabinet': k}, {'$set': v})
    elif msg.topic == 'cabinet':
        print(requests.post(
            "https://fcm.googleapis.com/fcm/send",
            headers={
                'Content-Type': 'application/json',
                'Authorization': f'key={os.environ.get("SERVER_TOKEN")}'
            },
            data=json.dumps({
                'notification': {
                    'title': '警告！！',
                    'body': '耗材櫃門並未緊閉，請派相關人員前往檢查！'
                },
                'to': os.environ.get('DEVICE_TOKEN'),
                'priority': 'high'
            })
        ).status_code)
    else:
        print(msg.topic)
        print(msg.payload.decode('utf-8'))


if __name__ == "__main__":
    # Set connect info
    client.connect("192.168.1.97", 1883, 60)

    # Start connect
    client.loop_forever()
