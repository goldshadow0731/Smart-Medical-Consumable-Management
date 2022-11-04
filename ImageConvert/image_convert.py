from argparse import ArgumentParser

from PIL import Image
import numpy as np


default_width = 640
default_height = 384

color_depth = 2

three_color = {
    'white': 0b11,
    'red': 0b01,
    'black': 0b00
}

two_color = {
    'white': 0b1,
    'black': 0b0
}


def convert_three_color(color_value, color_reverse, threshold=128):
    if np.dot(color_value, [0.299, 0.587, 0.114]) > threshold:
        return three_color['black'] if color_reverse else three_color['white']
    elif color_value[0] > threshold:
        return three_color['red']
    else:
        return three_color['white'] if color_reverse else three_color['black']


if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument('image')  # image path
    parser.add_argument('-q', '--quarter', action='store_true',
                        help='quarter image')  # default False
    parser.add_argument('--reverse', action='store_true',
                        help='reverse color')  # default False

    args = parser.parse_args()

    # open image and convert to array
    img = Image.open(args.image)
    if args.quarter is True:
        img = img.resize((default_width//2, default_height//2))
    else:
        img = img.resize((default_width, default_height))
    img_array = np.asarray(img)  # (image_height, image_width, image_channel)
    print(img_array.shape)

    image_length = img_array.shape[0]*img_array.shape[1]*color_depth//8
    convert_img = np.zeros((image_length, ), dtype=np.int32)
    for i in range(img_array.shape[0]):
        for j in range(0, img_array.shape[1], 8//color_depth):
            temp_val = 0
            for k in range(8//color_depth):
                temp_val |= convert_three_color(
                    img_array[i][j+k], args.reverse)
                temp_val <<= color_depth
            temp_val >>= color_depth
            convert_img[(i*img_array.shape[1]+j)//4] = temp_val

    # .h file
    with open("image.h", 'w') as fp:
        fp.write('extern const unsigned char MY_IMAGE[];\n')

    # .cpp file
    with open("image.cpp", 'w') as fp:
        fp.write('#include "image.h"\n')
        fp.write('#include <avr/pgmspace.h>\n\n')

        fp.write(
            "const unsigned char MY_IMAGE[%d] PROGMEM = {\n" % image_length)
        for index in range(0, convert_img.shape[0], 16):
            img_val_list = list()
            for img_val in convert_img[index: index+16]:
                val = hex(img_val).split('0x')[1].upper()
                if len(val) < 2:
                    val = f"0{val}"
                img_val_list.append(f"0X{val}")
            fp.write(",".join(img_val_list) + ",\n")
        fp.write("};\n")
