import pandas as pd
import numpy as np


file_path = "./coreset_benchmark.csv"


# Header
# coreset_size,features,rounds,d1,var1_sq,dev,d2,var2_sq,dev2,diff,sum

df = pd.read_csv(file_path) 


# group by (coreset_size, features) (mean all columns)
grouped = df.groupby(['coreset_size', 'features']).mean().reset_index()

# plot 3d  X: # coreset_size, Y: features, Z: d1 +- dev

import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')
x = grouped['coreset_size']
y = grouped['features']
z1 = grouped['d1']
z2 = grouped['d2']

ax.plot_trisurf(x, y, z1, color='blue', alpha=0.8, label='d1')
ax.plot_trisurf(x, y, z2, color='red', alpha=0.2, label='d2')

plt.title('3D Plot of Coreset Benchmark')
ax.set_xlabel('Coreset Size')
ax.set_ylabel('Features')
ax.set_zlabel('d1')
ax.legend()
plt.show()