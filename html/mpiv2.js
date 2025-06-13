

async function master(opt, topo, rm) {
    let reqs = 
        Array.from({ length: process - 1 }, (_, i) => {
            return addUUID(recv(0));
        });


    while (true) {        
        let [uuid, id] = await Promise.any(reqs.map((r) => r[1]));
        reqs = reqs.filter((r) => r[0] !== uuid);

        if (typeof id === "number") {
            reqs.push(addUUID(send(0, id, 0)));
        } else {
            reqs.push(addUUID(recv(0)));
        }
    }
}

async function worker(rank, topo, rm) {
    const current_node = `node-0-${rank}`;
    const current_node_element = document.getElementById(current_node);
    const baseBackgroundColor = current_node_element.style.backgroundColor;


    const store = {}

    function hasPrimeSupportFor(R) {
        return rm[R] && rm[R].some((r) => r === rank);
    }


    // let master_send = send(rank, 0, rank);
    let peer_reqs = Array.from({ length: process - 1 }, (_, i) => {
        return addUUID(recv(rank));
    });

    let master_send = send(rank, 0, rank);

    while (true) {
        let R = -1;

        while (true) {

            const [uuid, r] = await Promise.any(peer_reqs.map((r) => r[1]));
            peer_reqs = peer_reqs.filter((r) => r[0] !== uuid);
            R = r;


            peer_reqs.push(addUUID(recv(rank)));
            if (R == 0) {
                await master_send;
                master_send = send(rank, 0, rank);
            }
    

            if(R in store) 
              break;  
            
            store[R] = true;
        }

        current_node_element.style.backgroundColor = "red";
        while (hasPrimeSupportFor(R)) {
            await do_work()
            store[R] = false;
            R++;
        }
        current_node_element.style.backgroundColor = baseBackgroundColor;

       
        const workers = rm[R] || [];
        if (workers.length > 0) {
            current_node_element.style.backgroundColor = "teal";
            rand_worker = workers[Math.floor(Math.random() * workers.length)];
            // await send(rank, rand_worker, R);
            send(rank, rand_worker, R);
            current_node_element.style.backgroundColor = baseBackgroundColor;
        }
    }
}

async function mpiv2(opt) {
    const rm = rankMap(process)
    const topo = document.CircularWithMasterTopology(container);
    document.init_comm_topology(topo);

    const proms = [master(opt, topo, rm)]
    for (let i = 1; i < process; i++) {
        proms.push(worker(i, topo, rm));
    }
    await Promise.all(proms)
}

function rankMap(workers) {
    const ranks = [];

    let worker_size = workers - 1;

    console.log("Worker size: ", worker_size);

    while (worker_size >= 1) {
        ranks.push(Array.from({ length: worker_size }, (_, i) => i + 1));
        worker_size = Math.floor(worker_size / 2);
    }

    return ranks;
}
