import argparse
import json
import math
import os
import time

import cv2
from dotenv import load_dotenv
from paho.mqtt import client as MQTT_Client
from pymongo import MongoClient

from epaper import GenerateEPaperImage


load_dotenv()


# MQTT
client = MQTT_Client.Client()


@client.connect_callback()
def on_connect(client, userdata, flags_dict, reason):
    print(f"========== {'Start Connect':^15s} ==========")

    client.subscribe("Error/#")


@client.message_callback()
def on_message(self, userdata, msg):
    print(f"{msg.topic:<10s} {msg.payload.decode('utf-8')}")


@client.disconnect_callback()
def on_disconnect(userdata, result_code):
    print(f"========== {'End Connect':^15s} ==========")


# MongoDB
mongodb = MongoClient(
    f'{os.environ.get("SERVER_PROTOCOL")}://{os.environ.get("MONGO_USER")}:{os.environ.get("MONGO_PASSWORD")}@{os.environ.get("SERVER")}')[os.environ.get("DATABASE")]


g_image = GenerateEPaperImage()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('cabinet', type=str,
                        help="Get cabinet data")  # image path
    args = parser.parse_args()

    cabinet = args.cabinet.upper()
    client.connect("192.168.1.97", 1883, 60)
    # client.loop_forever()

    count = 0
    while True:
        try:
            count += 1
            img = g_image.gen_image(
                mongodb['Inventory_Data'].find_one({"cabinet": cabinet}))
            # cv2.imwrite("photo.jpg", img)
            # cv2.imshow("Photo", img)
            # cv2.waitKey(0)
            # cv2.destroyAllWindows()
            data = g_image.convert_image_to_data(img)
            data_list = g_image.convert_data_to_unicode(data)

            print(f"{count:>4d} Start")
            stride = 60
            client.publish(
                f"draw{cabinet}/0000",
                json.dumps({
                    "stride": str(stride),
                    "package": str(math.ceil(len(data_list) / stride))
                })
            )
            time.sleep(1)
            for index, data_index in enumerate(range(0, len(data_list), stride), start=1):
                # print(f"Publish: {index:>4d}")
                client.publish(
                    f'draw{cabinet}/{index:04d}',
                    "".join(data_list[data_index:data_index+stride])
                )
                time.sleep(0.01)
            print(f"{count:>4d} End")
            time.sleep(60)
        except KeyboardInterrupt:
            break
