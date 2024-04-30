#include <fcntl.h>
#include <routingkit/contraction_hierarchy.h>
#include <routingkit/inverse_vector.h>
#include <routingkit/min_max.h>
#include <routingkit/timer.h>
#include <routingkit/vector_io.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <stdexcept>
#include <vector>

#include "verify.h"

using namespace RoutingKit;
using namespace std;

inline uint32_t hash32_2(uint32_t a) {
  uint32_t z = (a + 0x6D2B79F5UL);
  z = (z ^ (z >> 15)) * (z | 1UL);
  z ^= z + (z ^ (z >> 7)) * (z | 61UL);
  return z ^ (z >> 14);
}

int main(int argc, char *argv[]) {
  try {
    string graph_first_out;
    string graph_head;
    string graph_weight;

    string ch_file;

    if (argc != 5) {
      cerr << argv[0] << " input_graph ch_file" << endl;
      return 1;
    } else {
      ch_file = argv[2];
    }

    size_t n, m;

    string filename(argv[1]);
    ifstream ifs(filename);
    vector<unsigned> first_out;
    vector<unsigned> head;
    vector<unsigned> weight;

    if (filename.find(".adj") != string::npos) {
      printf("Reading pbbs format...\n");
      string header;
      ifs >> header;
      ifs >> n >> m;
      first_out = vector<unsigned>(n + 1);
      head = vector<unsigned>(m);
      weight = vector<unsigned>(m);
      for (size_t i = 0; i < n; i++) {
        ifs >> first_out[i];
      }
      first_out[n] = m;
      for (size_t i = 0; i < m; i++) {
        ifs >> head[i];
      }
      for (size_t i = 0; i < m; i++) {
        double st;
        ifs >> st;
        weight[i] = (int)st;
      }
    } else if (filename.find(".bin") != string::npos) {
      printf("Reading binary format...\n");
      struct stat sb;
      int fd = open(argv[1], O_RDONLY);
      if (fd == -1) {
        std::cerr << "Error: Cannot open file " << argv[1] << std::endl;
        abort();
      }
      if (fstat(fd, &sb) == -1) {
        std::cerr << "Error: Unable to acquire file stat" << std::endl;
        abort();
      }
      char *data = static_cast<char *>(
          mmap(0, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
      size_t len = sb.st_size;
      n = reinterpret_cast<uint64_t *>(data)[0];
      m = reinterpret_cast<uint64_t *>(data)[1];
      first_out = vector<unsigned>(n + 1);
      head = vector<unsigned>(m);
      weight = vector<unsigned>(m);
      for (size_t i = 0; i < n + 1; i++) {
        first_out[i] = reinterpret_cast<uint64_t *>(data + 3 * 8)[i];
      }
      constexpr int LOG2_WEIGHT = 5;  // 18
      constexpr int WEIGHT = 1 << LOG2_WEIGHT;
      for (size_t i = 0; i < n; i++) {
        for (size_t j = first_out[i]; j < first_out[i + 1]; j++) {
          head[j] = reinterpret_cast<uint32_t *>(data + 3 * 8 + (n + 1) * 8)[j];
          weight[j] = ((hash32_2(i) ^ hash32_2(head[j])) & (WEIGHT - 1)) + 1;
        }
      }
      if (data) {
        const void *b = data;
        munmap(const_cast<void *>(b), len);
      }
    } else {
      std::cerr << "Unsupported file extension\n";
      abort();
    }
    ifs.close();

    cout << "done" << endl;

    cout << "Validity tests ... " << flush;
    check_if_graph_is_valid(first_out, head);
    cout << "done" << endl;

    const unsigned node_count = first_out.size() - 1;
    const unsigned arc_count = head.size();

    if (first_out.front() != 0)
      throw runtime_error("The first element of first out must be 0.");
    if (first_out.back() != arc_count)
      throw runtime_error(
          "The last element of first out must be the arc count.");
    if (head.empty()) throw runtime_error("The head vector must not be empty.");
    if (max_element_of(head) >= node_count)
      throw runtime_error("The head vector contains an out-of-bounds node id.");
    if (weight.size() != arc_count)
      throw runtime_error(
          "The weight vector must be as long as the number of arcs");

    auto ch = ContractionHierarchy::build(
        node_count, invert_inverse_vector(first_out), head, weight,
        [](string msg) { cout << msg << endl; });
    check_contraction_hierarchy_for_errors(ch);
    ch.save_file(ch_file);

  } catch (exception &err) {
    cerr << "Stopped on exception : " << err.what() << endl;
  }
}

