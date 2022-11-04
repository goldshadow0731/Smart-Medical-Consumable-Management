import cv2
import numpy as np


width = 640
height = 384

img = np.zeros((height, width, 3), np.uint8)
img.fill(255)
cv2.rectangle(img, (0, 0), (width, height), (0, 0, 0), 20)
cv2.putText(img, "Error", (0, 280), cv2.FONT_HERSHEY_SIMPLEX, 8,
            (0, 0, 255), 20, cv2.LINE_AA)

# cv2.imwrite("error.jpg", img)
# cv2.imshow("Photo", img)
# cv2.waitKey(0)
# cv2.destroyAllWindows()
