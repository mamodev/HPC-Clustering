import matplotlib.pyplot as plt
from sklearn.datasets import make_blobs
from sklearn.decomposition import PCA
import numpy as np
import time


DIMS = 30
SAMPLES = 10000
K = 3

CORESET_SIZE = 100
WORKING_SET_SIZE = CORESET_SIZE * 2


XX, YY = make_blobs(n_samples=SAMPLES, centers=K, n_features=DIMS, random_state=42)

# XX3D = PCA(n_components=3).fit_transform(XX)
XXPCA = PCA(n_components=3).fit(XX)
XX3D = XXPCA.transform(XX)

fig = plt.figure(figsize=(10, 10))
ax = fig.add_subplot(111, projection='3d')
ax.scatter(XX3D[:, 0], XX3D[:, 1], XX3D[:, 2], c=YY, s=10, cmap='viridis', alpha=0.1, label='Data Points')
plt.show(block=False)


indices = np.random.choice(SAMPLES, WORKING_SET_SIZE, replace=False)
WXX = XX[indices]
WXX3D = XX3D[indices]
WYY = YY[indices]

ax.scatter(WXX3D[:, 0], WXX3D[:, 1], WXX3D[:, 2], c='red', s=20, label='Working Set')

plt.pause(1)
input("Press Enter to continue...")

ax.clear()



COLORS = ['red', 'green', 'blue', 'orange', 'purple', 'cyan', 'magenta', 'yellow', 'pink', 'brown', 'gray', 'olive', 'teal', 'navy', 'maroon', 'lime']
COLORS = set(COLORS) 

class Node: 
    def __init__(self, point, centroid):
        self.color = np.random.choice(list(COLORS))
        COLORS.remove(self.color)

        self.centroid = centroid
        self.points = point
        self.left = None
        self.right = None

    def is_leaf(self):
        return self.left is None and self.right is None
    
    def cost(self):
        if self.is_leaf():
            # return np.sum(np.linalg.norm(self.points - self.centroid, axis=1) ** 2)
            # use cosine similarity to compute cost instead
            return np.sum(
                1
                - np.einsum("ij,j->i", self.points, self.centroid.squeeze())
                / (
                    np.linalg.norm(self.points, axis=1)
                    * np.linalg.norm(self.centroid)
                )
            )


        return self.left.cost() + self.right.cost()

    def choose_leaf(self):
        if self.is_leaf():
            return self

        left_prob = self.left.cost() / (self.cost())
        if np.random.rand() < left_prob:
            return self.left.choose_leaf()
        else:
            return self.right.choose_leaf()
        
    def pick_far_away_point(self):
        assert self.is_leaf(), "Can only pick a point from a leaf node"

        distances = 1 - np.einsum("ij,j->i", self.points, self.centroid.squeeze()) / (
            np.linalg.norm(self.points, axis=1) * np.linalg.norm(self.centroid)
        )

        farthest_point_index = np.argmax(distances)
        farthest_point = self.points[farthest_point_index]
        
        return farthest_point

    def split(self, farthest_point):
        left_center = self.centroid
        right_center = farthest_point

        # left_points = self.points[np.linalg.norm(self.points - left_center, axis=1) < np.linalg.norm(self.points - right_center, axis=1)]
        # right_points = self.points[np.linalg.norm(self.points - left_center, axis=1) >= np.linalg.norm(self.points - right_center, axis=1)]

        epsilon = 1e-9

        # Partition for left_points
        left_points = self.points[
            (
                np.sum(self.points * left_center, axis=1)
                / (
                    np.linalg.norm(self.points, axis=1)
                    * np.linalg.norm(left_center)
                    + epsilon
                )
            )
            > (
                np.sum(self.points * right_center, axis=1)
                / (
                    np.linalg.norm(self.points, axis=1)
                    * np.linalg.norm(right_center)
                    + epsilon
                )
            )
        ]

        # Partition for right_points
        right_points = self.points[
            (
                np.sum(self.points * left_center, axis=1)
                / (
                    np.linalg.norm(self.points, axis=1)
                    * np.linalg.norm(left_center)
                    + epsilon
                )
            )
            <= (
                np.sum(self.points * right_center, axis=1)
                / (
                    np.linalg.norm(self.points, axis=1)
                    * np.linalg.norm(right_center)
                    + epsilon
                )
            )
        ]


        self.left = Node(left_points, left_center)
        self.right = Node(right_points, right_center)
        self.points = None  
        COLORS.add(self.color)
        self.color = None

    def flat_leaves(self):
        if self.is_leaf():
            return [self]
        
        return self.left.flat_leaves() + self.right.flat_leaves()

random_point = np.random.choice(WXX.shape[0], 1)
root = Node(WXX, WXX[random_point])

for k in range(120):
    leaf = root.choose_leaf()
    farthest_point = leaf.pick_far_away_point()
    leaf.split(farthest_point)

    farthest_point = XXPCA.transform(farthest_point.reshape(1, -1))[0]
    ax.scatter(farthest_point[0], farthest_point[1], farthest_point[2], c='red', s=100, label='Farthest Point' if k == 0 else "", marker='x')
    plt.pause(.4)

    ax.clear()
    leaves = root.flat_leaves()
    for i, leaf in enumerate(leaves):
        centroid = XXPCA.transform(leaf.centroid.reshape(1, -1))[0]
        ax.scatter(centroid[0], centroid[1], centroid[2], c=leaf.color, s=100, label='Centroid' if k == 0 else "")

        if leaf.is_leaf():
            points_3D = XXPCA.transform(leaf.points)
            ax.scatter(points_3D[:, 0], points_3D[:, 1], points_3D[:, 2], c=leaf.color, s=20, alpha=1)

    plt.pause(.4)


plt.show(block=True)