import os
import numpy as np
import matplotlib.pyplot as plt
import pandas as pd


# list all files in .data with format coreset_*.csv
files = os.listdir('.data')
files = [f for f in files if f.startswith('coreset_') and f.endswith('.csv')]

# file format is coreset_<number>.csv, extract the number and sort the files
files = sorted(files, key=lambda x: int(x.split('_')[1].split('.')[0]), reverse=True)


fig = plt.figure(figsize=(10, 10))
# create 3D axis
ax = fig.add_subplot(111, projection='3d')

data = []

for f in files:
    # csv file format: w,x1,x2,...,xn
    # w: weight of the point
    # x1,x2,...,xn: coordinates of the point
    # Scatter points with weights (size of the points) 

    df = pd.read_csv(os.path.join('.data', f))
    WW = df.iloc[:, 0].values
    XX = df.iloc[:, 1:].values
    assert XX.shape[1] == 3, "The data must be 3D"
    data.append((WW, XX))

# normalize weights
MAX_W = max([np.max(WW) for WW, XX in data])
MIN_W = min([np.min(WW) for WW, XX in data])

# normalize weights to [0, 1]
for i in range(len(data)):
    WW, XX = data[i]
    WW = (WW - MIN_W) / (MAX_W - MIN_W)
    data[i] = (WW, XX)

# plot points
for i in range(len(data)):
    WW, XX = data[i]

    t = ((i + 1) / len(data))

    alpha = (1 - t) * 0.5 + t * 0.8
    colorRed = (1 - t) * 1 + t * 0
    colorGreen = (1 - t) * 1 + t * 0
    colorBlue = (1 - t) * 1 + t * 0

    color = (colorRed, colorGreen, colorBlue)


    ax.scatter(XX[:, 0], XX[:, 1], XX[:, 2], s=WW * 100, alpha=alpha,
                color=color, edgecolor='k', linewidth=0.5,
               label=f'coreset {i+1}')



plt.title('Coreset points')
plt.xlabel('X1')
plt.ylabel('X2')
ax.set_zlabel('X3')
plt.legend()
plt.show()




