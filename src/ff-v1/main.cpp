#include <iostream>
#include "ff/ff.hpp"
#include "parser.hpp"

using namespace ff;

typedef std::vector<float> fftask_t;
 
struct mem_source_t : ff_node_t<fftask_t> {
    MemoryStream<false> mem_stream;
    mem_source_t(int argc, char *argv[]) : ff_node_t<fftask_t>(), mem_stream(argc, argv) {}

    fftask_t *svc(fftask_t *t) {

        std::vector<float> batch = mem_stream.next_batch();
        while (!batch.empty()) {
            fftask_t *task = new fftask_t(std::move(batch));
            ff_send_out(task);
            batch = mem_stream.next_batch();
        }

        return EOS; // End-Of-Stream
    }
};
fftask_t* secondStage(fftask_t *t, ff_node * const node) {
    std::cout << "Hello I'm stage" << node->get_my_id() << "\n";
    return t;
}

struct thirdStage: ff_node_t<fftask_t> {
    fftask_t *svc(fftask_t *t) {
	// std::cout << "stage" << get_my_id() << " received " << *t << "\n";
    std::cout << "stage" << get_my_id() << " received task of size " << t->size() << "\n";
        delete t;
	return GO_ON;
    }
};
int main(int argc, char *argv[]) {

    ff_Farm<> farm(make_unique<ff_node_F<fftask_t> >(secondStage), 3, true); // true means input channel

    ff_Pipe<> pipe(
                   make_unique<mem_source_t>(argc, argv),
                   farm,
                   make_unique<thirdStage>()
                );

    if (pipe.run_and_wait_end()<0) {

    }

    return 0;
}