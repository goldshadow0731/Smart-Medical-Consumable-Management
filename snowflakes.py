import json
import time


EPOCH_TIMESTAMP = 946656000  # 2000年01月01日00點00分00秒


class UIDGenerator(object):
    def __init__(self, device: int):
        # 1 bit 0 + 39 bit timestamp + 24 bit device = 64 bit (8 Byte)
        # 174.21 y and 16777215 device
        with open("./device.json") as fp:
            data = json.load(fp)
        self.node_id = data.get(str(device))
        if self.node_id is None:
            self.node_id = len(data.values())
            data.update({device: self.node_id})
            with open("./device.json", "w") as fp:
                json.dump(data, fp, ensure_ascii=False, indent=4)
        self.last_timestamp = EPOCH_TIMESTAMP

    def get_id(self) -> int:
        curr_time = int(time.time() * 100)
        if curr_time < self.last_timestamp:
            raise Exception("Time error")
        elif curr_time > self.last_timestamp:
            self.last_timestamp = curr_time

        generated_id = ((curr_time - EPOCH_TIMESTAMP) << 24) | self.node_id
        return generated_id

    @property
    def state(self):
        return {
            "node_id": self.node_id,
            "last_timestamp": self.last_timestamp
        }
