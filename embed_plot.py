import numpy as np
import matplotlib.pyplot as plt
from sklearn.decomposition import PCA


fig=None
terminated = False

def on_close(event):
    global fig, terminated
    if fig is not None:
        plt.close(fig)
        fig = None
    terminated = True


def exit():
    global fig, terminated
    if fig is not None:
        plt.close(fig)
        fig = None
    terminated = True

# take variadic arguments for dataset
def plot(max_per_col, *args):
    global fig, terminated

    if terminated:
        return -1
    
    # check that each argument is a numpy array in the shape (n_samples, n_features)
    for arg in args:
        if not isinstance(arg, np.ndarray) or arg.ndim != 2:
            return -1
        
    # create a figure with 3D subplots
    num_plots = len(args)
    cols = num_plots if num_plots <= max_per_col else max_per_col
    rows = (num_plots + cols - 1) // cols  # Ceiling division

    if fig is None:
        fig = plt.figure(figsize=(20, 10))
        fig.canvas.mpl_connect('close_event', on_close)
        plt.show(block=False)
    else:
        fig.clf()

    axes = [fig.add_subplot(rows, cols, i + 1, projection='3d' if args[i].shape[1] > 2 else None) for i in range(num_plots)]

    for i, ax in enumerate(axes):
        # layout [weight, f1, f2, ... , fN]
        data = args[i]
        weights = data[:, 0] 
        data = data[:, 1:]

        min_weight = np.min(weights)
        max_weight = np.max(weights)
        if max_weight == min_weight: # Avoid division by zero if all weights are the same
            scaled_weights = np.full_like(weights, 50) # Use a default size
        else:
            scaled_weights = 10 + (weights - min_weight) / (max_weight - min_weight) * 190

        # reduce to 3D if more than 3 dimensions
        if data.shape[1] > 3:
            pca = PCA(n_components=3)
            data = pca.fit_transform(data)

        if data.shape[1] == 3:
            ax.scatter(data[:, 0], data[:, 1], data[:, 2], alpha=0.5, edgecolor='k', linewidth=0.1, s=scaled_weights)
            ax.set_title(f'Plot {i + 1}')
            ax.set_xlabel('X1')
            ax.set_ylabel('X2')
            ax.set_zlabel('X3')
        else:
            ax.scatter(data[:, 0], data[:, 1], alpha=0.5, edgecolor='k', linewidth=0.1, s=scaled_weights)
            ax.set_title(f'Plot {i + 1}')
            ax.set_xlabel('X1')
            ax.set_ylabel('X2')

    plt.tight_layout()
    fig.canvas.draw_idle()
    fig.canvas.flush_events()
    plt.pause(0.01)  # Allow the plot to update

    return 0

  