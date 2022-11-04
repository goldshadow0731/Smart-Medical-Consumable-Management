from typing import Text, List, Dict

import cv2
import numpy as np


class GenerateEPaperImage():
    line_width: int = 10
    text_max_length: int = 16
    color_depth: int = 2  # bit
    color: dict = {
        'white': 0b11,
        'red': 0b01,
        'black': 0b00
    }

    def __init__(self, width: int = 640, height: int = 384):
        self.width = width
        self.height = height

    # Notice
    @classmethod
    def convert_color(cls, color_value: List[int], threshold: int = 128) -> int:
        if color_value[0] > threshold and color_value[1] > threshold and color_value[2] > threshold:
            return cls.color['white']
        elif color_value[2] > threshold:
            return cls.color['red']
        else:
            return cls.color['black']

    @classmethod
    def split_text(cls, raw_text: Text) -> List[Text]:
        text_list = [""]
        for word in raw_text.split("_"):
            word += " "
            if len(text_list[-1] + word) >= cls.text_max_length:
                text_list.append(word)
            else:
                text_list[-1] += word
        return [word.strip() for word in text_list]

    def gen_image(self, data: Dict) -> np.ndarray:
        cabinet_width = self.width // len(data["position"])
        cabinet_height = self.height // max([len(i) for i in data["position"]])

        cabinet = list()
        for row_data in data["position"]:
            cabinet_row = list()
            for col_data in row_data:
                # Generate an image
                img = np.zeros((cabinet_height, cabinet_width, 3), np.uint8)
                img.fill(255)
                cv2.rectangle(img, (0, 0), (cabinet_width, cabinet_height),
                              (0, 0, 0), self.line_width)

                # Write text
                text_list = self.split_text(col_data)
                text_list.reverse()
                for index, word in enumerate(text_list):
                    cv2.putText(
                        img, word, (
                            self.line_width*2, cabinet_height//2-self.line_width*2-index*40
                        ),
                        cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 0), 2, cv2.LINE_AA
                    )
                cv2.putText(
                    img, str(data["num"][col_data]), (
                        self.line_width*2, cabinet_height-self.line_width*2
                    ),
                    cv2.FONT_HERSHEY_SIMPLEX, 2, (0, 0, 0), 2, cv2.LINE_AA
                )
                cabinet_row.append(img)

            # Padding width
            width_mod = self.width % len(data["position"])
            if width_mod > 0:
                img = np.zeros((cabinet_height, width_mod, 3), np.uint8)
                img.fill(0)
                cabinet_row.append(img)
            cabinet.append(cv2.hconcat(cabinet_row))

        # Padding height
        height_mod = self.height % max([len(i) for i in data["position"]])
        if height_mod > 0:
            img = np.zeros((height_mod, self.width, 3), np.uint8)
            img.fill(0)
            cabinet.append(img)
        cabinet_img = cv2.vconcat(cabinet)

        return cabinet_img

    @classmethod
    def convert_image_to_data(cls, image: np.ndarray) -> np.ndarray:
        step = 8 // cls.color_depth
        image_length = image.shape[0] * image.shape[1] // step

        img_data = np.zeros((image_length, ), dtype=np.uint8)
        for i in range(image.shape[0]):
            for j in range(0, image.shape[1], step):
                val = 0
                for k in range(step):
                    val |= cls.convert_color(image[i][j+k])
                    val <<= cls.color_depth
                val >>= cls.color_depth
                img_data[(i*image.shape[1]+j)//step] = val

        return img_data

    @staticmethod
    def convert_data_to_unicode(data: np.ndarray) -> List[Text]:
        return [chr(val) for val in data]

    @staticmethod
    def save_to_cpp_file(image):
        data_list = list()
        for px_val in image:
            val = hex(px_val).split('0x')[1].upper()
            if len(val) < 2:
                val = f"0{val}"
            data_list.append(f"0X{val}")
        with open("image.h", "w") as fp:
            fp.write('extern const unsigned char MY_IMAGE[];\n')
        # const unsigned char MY_IMAGE[61440] PROGMEM =
        with open("image.cpp", "w") as fp:
            fp.write('#include "image.h"\n\n')
            fp.write('const unsigned char MY_IMAGE[61440] PROGMEM = {\n')
            fp.write(",\n".join(
                [",".join(data_list[i:i+16])
                 for i in range(0, len(data_list), 16)]
            ))
            fp.write('};\n')
