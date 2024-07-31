import pandas as pd
import numpy as np
from scipy import stats
import random

u1 = 0  # 第一个高斯分布的均值
sigma1 = 0.3  # 第一个高斯分布的标准差

x = np.arange(-1.152 * 2, 1.152 * 2, 0.002)
# 表示第一个高斯分布函数
y1 = np.multiply(np.power(np.sqrt(2 * np.pi) * sigma1, -1), np.exp(-np.power(x - u1, 2) / 2 * sigma1 ** 2))

y1 = y1 / max(y1)

df = pd.DataFrame(y1)
df.to_csv('block_difference.csv')

