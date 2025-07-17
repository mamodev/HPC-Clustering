class ReactiveBuckets {
  constructor() {
    this.buckets = {};
  }

  init() {
    const box = document.createElement("div");
    box.id = "reactive-buckets";
    box.style.display = "flex";
    box.style.flexDirection = "row";
    box.style.backgroundColor = "lightgrey";
    box.style.height = "100px";
    box.style.alignItems = "center";
    this.box = box;

    for (let i = 0; i < 16; i++) {
      this.buckets[i] = 0; // Initialize buckets
      this.createRank(i); // Create rank elements
    }

    return box;
  }

  createRank(rank) {
    const rankDiv = document.createElement("div");
    rankDiv.id = `node-1-${rank}`; // Unique ID for each rank
    rankDiv.style.display = "flex";
    rankDiv.style.justifyContent = "center";
    rankDiv.style.alignItems = "center";
    rankDiv.style.width = "50px";
    rankDiv.style.height = "50px";
    rankDiv.style.border = "2px solid grey";
    rankDiv.style.margin = "8px";
    rankDiv.style.padding = "8px";
    rankDiv.style.backgroundColor = "white";
    rankDiv.textContent = `Rank ${rank}`;
    rankDiv.style.fontSize = "16px";
    rankDiv.style.fontWeight = "bold";

    this.box.appendChild(rankDiv);
  }

  addRank(rank) {
    this.buckets[rank] = 1;
    const rankDiv = document.getElementById(`rank-${rank}`);
    if (rankDiv) {
      rankDiv.style.backgroundColor = "lightgreen"; // Change color to indicate processing
    }
  }

  removeRank(rank) {
    if (rank in this.buckets) {
      const rankDiv = document.getElementById(`rank-${rank}`);
      if (rankDiv) {
        rankDiv.style.backgroundColor = "white"; // Change color to indicate processing
      }

      this.buckets[rank] = 0; // Reset the bucket
    }
  }

  mark_input(in1, in2) {
    const rankDiv1 = document.getElementById(`rank-${in1}`);
    const rankDiv2 = document.getElementById(`rank-${in2}`);
    if (rankDiv1) {
      rankDiv1.style.backgroundColor = "lightblue"; // Change color to indicate input
    }
    if (rankDiv2) {
      rankDiv2.style.backgroundColor = "lightblue"; // Change color to indicate input
    }
  }

  has(rank) {
    return this.buckets.hasOwnProperty(rank) && this.buckets[rank] > 0;
  }
}

function createTopology() {
  const div = document.createElement("div");

  div.id = "topology";
  div.style.display = "flex";
  div.style.flexDirection = "row";
  div.style.justifyContent = "center";
  div.style.alignItems = "center";
  div.style.width = "100%";
  div.style.height = "100%";

  const n1 = document.createNode(0, 0);
  const n2 = document.createNode(0, 1);

  n1.style.margin = "100px";
  n2.style.margin = "100px";

  div.appendChild(n1);
  div.appendChild(n2);

  return [div, [2, 16]];
}

async function source() {
  while (true) {
    await send(0, 1, 0);
  }
}

async function main() {
  const buckets = new ReactiveBuckets();
  container.appendChild(buckets.init());
  container.style.flexDirection = "column-reverse";

  const node = document.getElementById("node-0-1");
  const baseColor = node.style.backgroundColor;

  while (true) {
    await recv(1);

    node.style.backgroundColor = "red"; // Indicate processing

    if (!buckets.has(0)) {
      buckets.addRank(0);
      await noop_cost();
    } else {
      let r = 0;
      while (buckets.has(r)) {
        buckets.mark_input(r, r + 1);

        await do_work(1);
        buckets.removeRank(r);

        r++;
      }

      node.style.backgroundColor = baseColor; // Reset color after processing

      buckets.addRank(r);
    }

    await new Promise((resolve) => setTimeout(resolve, 100)); // Simulate processing time
  }
}

async function serial() {
  const [topoEl, topo] = createTopology();
  document.init_comm_topology(topo);
  container.appendChild(topoEl);

  const p = [source(), main()];
  await Promise.all(p);
  console.log("All tasks completed");
}
