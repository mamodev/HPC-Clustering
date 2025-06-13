

// async function ring2() {
//   const topo = document.CircularWithMasterTopology(container);
//   document.init_comm_topology(topo);

//   async function master() {
//     const chunk = `: )`;

//     let reqs = Array.from({ length: process - 1 }, (_, i) => {
//       return addUUID(recv(0));
//     });

    
//     while (true) {
//       let [uuid, id] = await Promise.any(reqs.map((r) => r[1]));
//       reqs = reqs.filter((r) => r[0] !== uuid);

//       if (typeof id === "number") {
//         reqs.push(addUUID(send(0, id, chunk)));
//         reqs.push(addUUID(recv(0)));
//       }
//     }

//     // Sync Send
//     // while (true) {
//     //   let [uuid, id] = await Promise.any(reqs.map((r) => r[1]));
//     //   reqs = reqs.filter((r) => r[0] !== uuid);
      
//     //   await send(0, id, chunk);
//     //   reqs.push(addUUID(recv(0)));
//     // }
//   }

//   async function worker(rank) {
//     const current_node = `node-0-${rank}`;

//     const current_node_element = document.getElementById(current_node);
//     const baseBackgroundColor = current_node_element.style.backgroundColor;


//     let avg_rcv_time = 0;
//     let avg_rcv_time_count = 0;
//     while (true) {
//       const t = new Date().getTime();
      
//       current_node_element.style.backgroundColor = "blue";
//       await send(rank, 0, rank);
//       current_node_element.style.backgroundColor = baseBackgroundColor;


//       const req = await recv(rank);
      
//       const elapsed = new Date().getTime() - t;
//       avg_rcv_time += elapsed;
//       avg_rcv_time_count += 1;

//       current_node_element.textContent = `Rcv: ${(avg_rcv_time / avg_rcv_time_count).toFixed(2)}ms`;

//       current_node_element.style.backgroundColor = "red";
//       await new Promise((resolve) =>
//         setTimeout(resolve, TCOMPUTE + TCOMPUTE * Math.random())
//       );
//       current_node_element.style.backgroundColor = baseBackgroundColor;
//     }
//   }

//   async function worker(rank) {
//     const current_node = `node-0-${rank}`;

//     const current_node_element = document.getElementById(current_node);
//     const baseBackgroundColor = current_node_element.style.backgroundColor;

//     // let out_send = send(rank, 0, rank);
//     // let in_req = recv(rank);

//     let MAX_OUTGOING = 3;
//     let MAX_INCOMING = 3;

//     let out_sends = 
//       Array.from({ length: MAX_OUTGOING }, () => addUUID(send(rank, 0, rank)));
    
//     let in_reqs =
//       Array.from({ length: MAX_INCOMING }, () => addUUID(recv(rank)));

//     avg_rcv_time = 0;
//     avg_rcv_time_count = 0;

//     while (true) {
//       const t = new Date().getTime(); 

//       current_node_element.style.backgroundColor = "blue";
//       let [uuid] = await Promise.any(out_sends.map((r) => r[1]));
//       out_sends = out_sends.filter((r) => r[0] !== uuid);
//       out_sends.push(addUUID(send(rank, 0, rank)));

//       current_node_element.style.backgroundColor = baseBackgroundColor;

//       [uuid, chunk] = await Promise.any(in_reqs.map((r) => r[1]));
//       in_reqs = in_reqs.filter((r) => r[0] !== uuid);
//       in_reqs.push(addUUID(recv(rank)));

//       const elapsed = new Date().getTime() - t;

//       avg_rcv_time += elapsed;
//       avg_rcv_time_count += 1;
//       current_node_element.textContent = `Rcv: ${(avg_rcv_time / avg_rcv_time_count).toFixed(2)}ms`;

      
//       current_node_element.style.backgroundColor = "red";
//       await new Promise((resolve) =>
//         setTimeout(resolve, TCOMPUTE + TCOMPUTE * Math.random())
//       );
//       current_node_element.style.backgroundColor = baseBackgroundColor;
//     }
//   }


//   const promises = [master()];
//   for (let i = 1; i < process; i++) {
//     promises.push(worker(i));
//   }

//   await Promise.all(promises);
// }

// ring2();

class NodeState {
  __state = {
    ready: false,
    saved_rank: new Set(),
  }

  set ready(value) {
    this.__state.ready = value;
    this.update();
  }

  add_rank(rank) {
    this.__state.saved_rank.add(rank);
    this.update();
  }

  remove_rank(rank) {
    this.__state.saved_rank.delete(rank);
    this.update();
  }

  get ready() {
    return this.__state.ready;
  }


  constructor(workerId) {
    this.workerId = workerId;
    this.Tr = document.createElement("tr");
    this.Tr.id = `NS-${workerId}`;
    this.update();
  }

  update() {
    this.Tr.innerHTML = `
      <td>${this.workerId}</td>
      <td id="NS-${this.workerId}-ready"
        style="color: ${this.ready ? "green" : "red"};"
      >${this.ready ? "Ready" : "Not Ready"}</td>

      <td id="NS-${this.workerId}-saved-ranks">
        ${Array.from(this.__state.saved_rank).join(", ") || "None"}
      </td>
    `;
  }
}

class StateManager {
  constructor(workers) {
    this.Table = document.createElement("table");
    this.Table.style.position = "absolute";
    this.Table.style.bottom = "0px";
    this.Table.style.right = "0px";

    this.HTable = document.createElement("thead");
    this.HTable.innerHTML = `
      <tr>
        <th>Worker ID</th>
        <th>State</th>
        <th>Saved Ranks</th>
      </tr>
    `;

    this.BTable = document.createElement("tbody");
    this.Table.appendChild(this.HTable);
    this.Table.appendChild(this.BTable);

    this.states = Array.from({ length: workers }, (_,i) => new NodeState(i));
    this.states.forEach((state) => {
      this.BTable.appendChild(state.Tr);
    });

    this.workers = workers;



  }

  

 
}


async function mpiv3() {
  const topo = document.CircularWithMasterTopology(container);
  document.init_comm_topology(topo);

  const sm = new StateManager(process);
  document.body.appendChild(sm.Table);

  async function master() {
    const chunk = `: )`;


    let reqs = Array.from({ length: process - 1 }, (_, i) => {
      return addUUID(recv(0));
    });


    while (true) {
      let [uuid, id] = await Promise.any(reqs.map((r) => r[1]));
      reqs = reqs.filter((r) => r[0] !== uuid);

      if (typeof id === "number") {
        sm.states[id].ready = true;
        // reqs.push(addUUID(send(0, id, chunk)));
      } else {
        reqs.push(addUUID(recv(0)));
      }


    }

  }

  async function worker(rank) {
    const current_node = `node-0-${rank}`;

    const current_node_element = document.getElementById(current_node);
    const baseBackgroundColor = current_node_element.style.backgroundColor;
    while (true) {
      
      current_node_element.style.backgroundColor = "blue";
      await send(rank, 0, rank);
      current_node_element.style.backgroundColor = baseBackgroundColor;
      const req = await recv(rank);
      
      
      current_node_element.style.backgroundColor = "red";
      await new Promise((resolve) =>
        setTimeout(resolve, TCOMPUTE + TCOMPUTE * Math.random())
      );
      current_node_element.style.backgroundColor = baseBackgroundColor;
    }
  }


  const promises = [master()];
  for (let i = 1; i < process; i++) {
    promises.push(worker(i));
  }

  await Promise.all(promises);
}

// mpiv3()




async function mpiv2 () {

}

