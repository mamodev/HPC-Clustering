window.addEventListener("focus", () => {
  window.location.reload();
});

function addUUID(promise, override = null) {
  const uuid = crypto.randomUUID();

  return [
    uuid,
    new Promise((resolve, reject) => {
      promise
        .then((value) => {
          if (override !== null) {
            resolve([uuid, override]);
          } else {
            resolve([uuid, value]);
          }
        })
        .catch(reject);
    }),
  ];
}

function observeStatus(promise) {
  let state = "pending"; // "pending" | "fulfilled" | "rejected"
  const tracked = promise.then(
    (v) => {
      state = "fulfilled";
      return v;
    },
    (e) => {
      state = "rejected";
      throw e;
    }
  );

  return {
    promise: tracked,
    getState: () => state,
    isPending: () => state === "pending",
    isFulfilled: () => state === "fulfilled",
    isRejected: () => state === "rejected",
    isDone: () => state !== "pending",
  };
}

globalThis.observeStatus = observeStatus;
globalThis.addUUID = addUUID;

document.createNode = function createNode(comm, idx) {
  const node = document.createElement("span");
  node.className = "node";
  node.id = `node-${comm}-${idx}`;
  node.textContent = `N[${idx}]`;
  return node;
};

async function message(node1Id, node2Id) {
  if (TSEND < 5) return new Promise((resolve) => setTimeout(resolve, TSEND));

  const n1r = document.getElementById(node1Id).getBoundingClientRect();
  const n2r = document.getElementById(node2Id).getBoundingClientRect();
  const crect = container.getBoundingClientRect();
  if (!n1r || !n2r) return;

  let px_msg_size = 0;
  const startX = n1r.left + n1r.width / 2 - px_msg_size / 2 - crect.left;
  const startY = n1r.top + n1r.height / 2 - px_msg_size / 2 - crect.top;
  const endX = n2r.left + n2r.width / 2 - px_msg_size / 2 - crect.left;
  const endY = n2r.top + n2r.height / 2 - px_msg_size / 2 - crect.top;

  // crete a line from start to end
  const l1 = document.createElement("div");
  const l2 = document.createElement("div");

  l1.position = "absolute";

  const x1 = [startX, startY];
  const x2 = [endX, endY];

  const angle = Math.atan2(endY - startY, endX - startX);
  const length = Math.sqrt((endX - startX) ** 2 + (endY - startY) ** 2);

  l1.style.width = `${length}px`;
  l1.style.height = "6px"; // or any desired thickness
  l1.style.backgroundColor = "red"; // or any desired color
  l1.style.position = "absolute";
  l1.style.left = `${startX}px`;
  l1.style.top = `${startY}px`;
  l1.style.transform = `rotate(${angle}rad)`;
  l1.style.transformOrigin = "0 0"; // Set the transform origin to the start point
  l1.style.transition = `transform ${TSEND}ms ease-in-out`;
  l1.style.zIndex = "-1000"; // Ensure the line is on top
  l1.style.opacity = "1"; // Start visible

  l2.style.width = `${length * 0.8}px`;
  l2.style.height = "6px"; // or any desired thickness
  l2.style.backgroundColor = "BLUE"; // or any desired color
  l2.style.position = "absolute";
  l2.style.left = `${startX}px`;
  l2.style.top = `${startY}px`;
  l2.style.transform = `rotate(${angle}rad)`;
  l2.style.transformOrigin = "0 0"; // Set the transform origin to the start point
  l2.style.transition = `transform ${TSEND}ms ease-in-out`;
  l2.style.zIndex = "-100"; // Ensure the line is on top
  l2.style.opacity = "1"; // Start visible

  container.appendChild(l1);
  container.appendChild(l2);

  return new Promise((resolve) => {
    setTimeout(() => {
      l1.remove();
      l2.remove();

      resolve();
    }, TSEND);
  });
}

document.msg = message;

document.init_comm_topology = function init_comm_topology(topology) {
  const process = topology.reduce((acc, val) => acc + val, 0);

  document.__ch = Array.from({ length: topology.length }, (_, i) => {
    // const messageDot = document.createElement("div");
    // messageDot.className = "message-dot";
    // messageDot.style.setProperty("--start-x", `${startX}px`);
    // messageDot.style.setProperty("--start-y", `${startY}px`);
    // messageDot.style.setProperty("--end-x", `${endX}px`);
    // messageDot.style.setProperty("--end-y", `${endY}px`);
    // messageDot.style.animation = `messageTravel ${TSEND}ms forwards ease-in-out`;
    // container.appendChild(messageDot);

    // return new Promise((resolve) => {
    //   messageDot.addEventListener(
    //     "animationend",
    //     () => {
    //       messageDot.remove();
    //       resolve();
    //     },
    //     { once: true }
    //   );
    // });

    return Array.from({ length: topology[i] }, (_, j) => {
      const channel = {
        queue: [],
        resolve: [],
      };

      channel.send = async (chunk, from) => {
        let res_ref = {};
        let prom = new Promise((resolve) => {
          res_ref.resolve = resolve;
        });

        if (!from) {
          throw new Error("from parameter is required");
        }

        channel.queue.push([chunk, from, res_ref]);

        if (channel.queue.length > process + 100) {
          throw new Error(
            `Queue for ${from} is too long: ${channel.queue.length}`
          );
        }

        if (channel.resolve.length > 0) {
          const res = channel.queue.shift();
          const resolv = channel.resolve.shift();
          await message(res[1], `node-${i}-${j}`);
          resolv(res);
        }

        return await prom;
      };

      channel.wait = async () => {
        // console.log("Waiting for message in channel:", channel.queue);

        let res = await new Promise(async (resolve) => {
          if (channel.queue.length > 0) {
            const res = channel.queue.shift();
            await message(res[1], `node-${i}-${j}`);
            resolve(res);
          } else {
            channel.resolve.push(resolve);
          }
        });

        res[2]?.resolve();

        return res[0];
      };

      return channel;
    });
  });
};

const decodeTarget = (target) => {
  let comm = -1;
  let idx = -1;

  if (typeof target === "string") {
    const parts = target.split("-");
    if (parts.length !== 3) {
      throw new Error(
        "Invalid target format. Expected 'node-comm-idx'. received: " + target
      );
    }
    comm = parseInt(parts[1], 10);
    idx = parseInt(parts[2], 10);
    if (isNaN(comm) || isNaN(idx)) {
      throw new Error("Invalid comm or idx in target format.");
    }
  } else if (Array.isArray(target)) {
    if (target.length !== 2) {
      throw new Error("Invalid target array format. Expected [comm, idx].");
    }
    comm = target[0];
    idx = target[1];
  } else if (typeof target === "number") {
    let acc = 0;
    for (let i = 0; i < document.__ch.length; i++) {
      if (acc + document.__ch[i].length > target) {
        comm = i;
        idx = target - acc;
        break;
      }
      acc += document.__ch[i].length;
    }

    if (comm === -1 || idx === -1) {
      throw new Error("Invalid target number. No matching communicator found.");
    }
  } else {
    console.error(
      "Invalid target type. Expected string, array, or number. received: " +
        target
    );
    throw new Error(
      "Invalid target type. Expected string, array, or number. received: " +
        typeof target
    );
  }

  if (comm < 0 || comm >= document.__ch.length) {
    throw new Error(
      `Invalid communicator index: ${comm}. Must be between 0 and ${
        document.__ch.length - 1
      }.`
    );
  }

  if (idx < 0 || idx >= document.__ch[comm].length) {
    throw new Error(
      `Invalid index: ${idx} for communicator ${comm}. Must be between 0 and ${
        document.__ch[comm].length - 1
      }.`
    );
  }

  return [comm, idx];
};

globalThis.send = (from, to, chunk) => {
  if (!document.__ch) {
    throw new Error(
      "Communicators not initialized. Call init_comm_topology first."
    );
  }

  const source = decodeTarget(from);
  const target = decodeTarget(to);

  return document.__ch[target[0]][target[1]].send(
    chunk,
    `node-${source[0]}-${source[1]}`
  );
};

globalThis.recv = async (node) => {
  if (!document.__ch) {
    throw new Error(
      "Communicators not initialized. Call init_comm_topology first."
    );
  }

  const target = decodeTarget(node);

  return document.__ch[target[0]][target[1]].wait();
};

// Graphical Process Distribution
document.BinTreePipelineTopology = function BinTreePipelineTopology(container) {
  communicators = Math.floor(Math.log2(process)) + 1;
  commSize = Array.from({ length: communicators }, (_, i) => {
    return Math.round(process / Math.pow(2, i + 1));
  });

  commSize = [1, ...commSize];

  commSize.forEach((size, index) => {
    const layer = document.createElement("div");
    layer.style.flex = 1;
    layer.style.boxSizing = "border-box";
    layer.style.display = "flex";
    layer.style.flexDirection = "column";

    for (let i = 0; i < size; i++) {
      const box = document.createElement("div");
      box.style.flex = 1;
      box.style.display = "flex";
      box.style.justifyContent = "center";
      box.style.alignItems = "center";
      box.style.boxSizing = "border-box";

      box.appendChild(document.createNode(index, i));
      layer.appendChild(box);
    }

    container.appendChild(layer);
  });

  return commSize;
};

document.CircularWithMasterTopology = function CircularWithMasterTopology(
  container
) {
  communicators = 1;
  commSize = [process];

  const outer_container = container;
  container = document.createElement("div");
  outer_container.appendChild(container);

  outer_container.style.justifyContent = "center";
  outer_container.style.alignItems = "center";
  outer_container.style.display = "flex";

  container.style.position = "relative";
  container.style.width = "925px";
  container.style.height = "925px";

  // dispose nodes as abs value in a ring

  for (let i = 1; i < process; i++) {
    const node = document.createNode(0, i);
    node.style.borderRadius = "50%";
    node.style.width = "100px";
    node.style.height = "100px";
    node.style.margin = "10px";
    node.style.display = "flex";
    node.style.zIndex = "1000";
    node.style.justifyContent = "center";
    node.style.alignItems = "center";
    node.style.backgroundColor = `hsla(${(i * 360) / process}, 80%, 70%, 0.3)`;
    node.style.position = "absolute";

    node.style.left = `${
      Math.cos((i * 2 * Math.PI) / (process - 1)) * 400 + 400
    } `;

    node.style.top = `${
      Math.sin((i * 2 * Math.PI) / (process - 1)) * 400 + 400
    }`;

    container.appendChild(node);
  }

  const masterNode = document.createNode(0, 0);
  masterNode.style.borderRadius = "50%";
  masterNode.style.width = "100px";
  masterNode.style.height = "100px";
  masterNode.style.margin = "10px";
  masterNode.style.display = "flex";
  masterNode.style.justifyContent = "center";
  masterNode.style.alignItems = "center";
  masterNode.style.backgroundColor = `hsl(0, 100%, 70%)`;
  masterNode.style.position = "absolute";
  masterNode.style.left = "400px";
  masterNode.style.top = "400px";
  container.appendChild(masterNode);

  return commSize;
};
