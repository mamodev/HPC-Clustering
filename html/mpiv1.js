async function master(options, topo) {
  const first_layer_size = Math.floor(topo[1]);

  if (options.some((o) => o.startsWith("master-send=async"))) {
    const reqs = Array.from({ length: first_layer_size }, (_, i) => {
      return addUUID(send(0, [1, i], `chunk`), [1, i]);
    });

    while (reqs.length > 0) {
      let [uuid, target] = await Promise.any(reqs.map((r) => r[1]));
      reqs.splice(
        reqs.findIndex((r) => r[0] === uuid),
        1
      );

      reqs.push(addUUID(send(0, target, `chunk`), target));
    }
  } else if (options.some((o) => o.startsWith("master-send=sync"))) {
    let next_worker = 0;
    while (true) {
      let chunk = ": )";
      await send(0, [1, next_worker], chunk);
      next_worker = (next_worker + 1) % first_layer_size;
    }
  } else {
    throw new Error("Invalid master-send option, please use 'async' or 'sync'");
  }
}

async function worker(
  options,
  rank,
  layer,
  layer_index,
  topo,
  eff_wnd = [],
  eff_wnd_size = 10
) {
  console.log(`Worker ${rank} in layer ${layer} started`);

  const terminationNeeded = topo[layer - 1];
  let recv_terminations = 0;

  const current_node_element = document.getElementById(
    `node-${layer}-${layer_index}`
  );

  if (!current_node_element)
    throw new Error(
      `Node element not found for layer ${layer}, index ${layer_index}`
    );

  const originalBg = current_node_element.style.backgroundColor;

  let in_reqs = [
    addUUID(recv([layer, layer_index])),
    addUUID(recv([layer, layer_index])),
  ];

  const MAX_OUTGOING = 3;
  let out_reqs = [];

  let chunks = [];

  const next_node_fn = outputNodeFn[
    options.find((o) => o.startsWith("next-node-fn=")).split("=")[1]
  ](topo, layer, layer_index);

  while (true) {
    let start_time = new Date().getTime();
    chunks = [];
    while (chunks.length < 2) {
      let [uuid, chunk] = await Promise.any(in_reqs.map((r) => r[1]));
      idx = in_reqs.findIndex((r) => r[0] === uuid);

      if (!chunk) {
        recv_terminations += 1;
        if (recv_terminations >= terminationNeeded) {
          console.log(`Worker ${rank} in layer ${layer} finished`);
          break;
        }
      } else {
        chunks.push(chunk);
      }

      in_reqs[idx] = addUUID(recv([layer, layer_index]));
    }

    const next_node = next_node_fn();

    current_node_element.style.backgroundColor = "red";
    let time = TCOMPUTE + TCOMPUTE * Math.random();
    if (next_node === null) time = time + TCOMPUTE + TCOMPUTE * Math.random();

    await new Promise((resolve) => setTimeout(resolve, time)); // simulate processing time
    current_node_element.style.backgroundColor = `${originalBg}`;

    if (out_reqs.length > MAX_OUTGOING) {
      current_node_element.style.backgroundColor = "blue";

      const [uuid, r] = await Promise.any(out_reqs.map((r) => r[1]));
      await r;
      out_reqs = out_reqs.filter((r) => r[0] !== uuid);

      current_node_element.style.backgroundColor = `${originalBg}`;
    }

    if (next_node) {
      out_reqs.push(addUUID(send([layer, layer_index], next_node, chunks[0])));
    }

    let delta = new Date().getTime() - start_time;

    eff_wnd.push(time / delta);
    if (eff_wnd.length > eff_wnd_size) {
      eff_wnd.shift();
    }

    let eff = eff_wnd.reduce((acc, val) => acc + val, 0) / eff_wnd.length;
    current_node_element.textContent = `${(eff * 100).toFixed(2)}%`;
  }

  return;
}

async function mpiv1(options) {
  if (!options) options = [];

  if (!options.some((o) => o.startsWith("master-send=")))
    options.push("master-send=async");

  if (!options.some((o) => o.startsWith("next-node-fn=")))
    options.push("next-node-fn=random");

  const topo = document.BinTreePipelineTopology(container);
  document.init_comm_topology(topo);
  const stats_text = createEfficiencyText();
  const options_text = createOptionsText(options);

  let w_eff_wnd = Array.from({ length: process + 1 }, () => []);
  const eff_wnd_size = 10;

  const interv = setInterval(() => {
    let eff =
      w_eff_wnd.flatMap((w) => w).reduce((acc, val) => acc + val, 0) /
      (w_eff_wnd.flatMap((w) => w).length || 1);

    stats_text.textContent = `Efficiency: ${(eff * 100).toFixed(2)}%`;
  }, 1000);

  const promises = [master(options, topo)];
  let acc = 0;
  for (let l = 1; l < topo.length; l++) {
    for (let i = 0; i < topo[l]; i++) {
      acc += 1;
      promises.push(
        worker(options, acc, l, i, topo, w_eff_wnd[acc], eff_wnd_size)
      );
    }
  }

  await Promise.all(promises);

  clearInterval(interv);
}

var outputNodeFn = {
  random: (topo, layer, layer_idx) => () =>
    layer + 1 >= topo.length
      ? null
      : [layer + 1, Math.floor(Math.random() * topo[layer + 1])],

  bintree: (topo, layer, layer_index) => () => {
    if (topo.length <= layer + 1) return () => null;
    if (topo[layer + 1] === 1) return () => [layer + 1, 0];

    if (topo[layer + 1] !== topo[layer] / 2 || topo[layer] % 2 !== 0) {
      alert("Your topology is not a perfect binary tree, please fix it.");
      throw new Error(
        `Next layer size ${topo[layer + 1]} does not match current layer size ${
          topo[layer]
        }`
      );
    }

    return () => [layer + 1, Math.floor(layer_index / 2)];
  },

  // "random+btree": () => {
  //   if (next_layer_size !== layer_size / 2 || layer_size % 2 !== 0)
  //     return outputNodeFn["random"]();
  //   return [next_layer, Math.floor(layer_index / 2)];
  // },

  // "rr+btree": () => {
  //   if (next_layer_size !== layer_size / 2 || layer_size % 2 !== 0)
  //     return outputNodeFn["round-robin"]();
  //   return [next_layer, Math.floor(layer_index / 2)];
  // },

  // "round-robin": (() => {
  //   let next_index = layer_index % next_layer_size;
  //   return () => {
  //     if (next_layer >= topo.length) return null;

  //     if (next_layer_size === 1) return [next_layer, 0];

  //     next_index = (next_index + 1) % next_layer_size;
  //     return [next_layer, next_index];
  //   };
  // })(),

  // "odd-even": () => {
  //   if (next_layer >= topo.length) return null;

  //   if (next_layer_size === 1) return [next_layer, 0];

  //   if (layer_index % 2 === 0) {
  //     return [next_layer, Math.floor(layer_index / 2)];
  //   } else {
  //     return [next_layer, Math.floor((layer_index - 1) / 2)];
  //   }
  // },
};

function createEfficiencyText() {
  const statsText = document.createElement("p");
  statsText.id = "stats";
  statsText.style.position = "absolute";
  statsText.style.top = "0px";
  statsText.style.left = "8px";
  statsText.style.zIndex = "1000";
  statsText.style.fontSize = "20px";
  statsText.style.fontWeight = "bold";
  statsText.style.fontFamily = "monospace";
  statsText.textContent = "Efficiency: 0%";
  statsText.style.backgroundColor = "rgba(255, 255, 255, 1)";
  statsText.style.padding = "8px";
  statsText.style.borderRadius = "8px";
  statsText.style.boxShadow = "0 0 10px rgba(0, 0, 0, 0.5)";

  document.body.appendChild(statsText);
  return statsText;
}

function createOptionsText(options) {
  const optionsText = document.createElement("p");
  optionsText.id = "options";
  optionsText.style.position = "absolute";
  optionsText.style.top = "0px";
  optionsText.style.right = "8px";
  optionsText.style.zIndex = "1000";
  optionsText.style.fontSize = "20px";
  optionsText.style.fontWeight = "bold";
  optionsText.style.fontFamily = "monospace";
  optionsText.textContent = `Options: ${options.join(", ")}`;
  optionsText.style.backgroundColor = "rgba(255, 255, 255, 1)";
  optionsText.style.padding = "8px";
  optionsText.style.borderRadius = "8px";
  optionsText.style.boxShadow = "0 0 10px rgba(0, 0, 0, 0.5)";

  document.body.appendChild(optionsText);
}
