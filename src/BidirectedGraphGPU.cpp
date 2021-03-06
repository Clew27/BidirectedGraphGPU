#include "BidirectedGraphGPU.hpp"

#include <functional>
#include "error.hpp"

BidirectedGraphGPU::BidirectedGraphGPU(HandleGraph& host_graph) {
    copy_to_GPU(host_graph);
}

#include <iostream>
void BidirectedGraphGPU::copy_to_GPU(HandleGraph& host_graph) {
    size = host_graph.get_node_count();

    /// Initialize node tracking lists
    h_neighbor_start = (nid_t*) malloc(2 * size * sizeof(nid_t) + 1); // Placeholder for cudaMalloc

    /// Get the size of adjacency 
    size_t edges_count = 0;
    host_graph.for_each_handle([&](const handle_t& handle) {
        edges_count += host_graph.get_degree(handle, false);
        edges_count += host_graph.get_degree(handle, true);
    });

    /// Populate GPU graph
    std::function<nid_t(const handle_t&)> handle_to_GPU_index = [&](const handle_t& handle) {
        nid_t gpu_idx = 2 * (host_graph.get_id(handle) - 1);
        if (!host_graph.get_is_reverse(handle)) gpu_idx += 1;
        return gpu_idx;
    };

    /// Stored as [1l, 1r, 2l, 2r, ...]
    h_adjacency = (nid_t*) malloc(edges_count * sizeof(nid_t)); // Placeholder for cudaMalloc
    /** Important:
     * Using the assumption that node ids begin at 1 and end at get_node_count().
     * This method is not tolerant of ids that skip. In a more robust 
     * implementation there will be a map from GPU indices to the original 
     * graph's node side.
     */
    int adjacency_i = 0;
    for (nid_t nid = 1; nid <= size; nid++) {
        handle_t handle = host_graph.get_handle(nid, true);

        h_neighbor_start[2 * (nid - 1)] = adjacency_i;
        host_graph.follow_edges(handle, false, [&](const handle_t& nei_handle) {
            h_adjacency[adjacency_i] = handle_to_GPU_index(nei_handle);
            adjacency_i++;
        });

        h_neighbor_start[2 * (nid - 1) + 1] = adjacency_i;
        host_graph.follow_edges(host_graph.flip(handle), false, [&](const handle_t& nei_handle) {
            h_adjacency[adjacency_i] = handle_to_GPU_index(nei_handle);
            adjacency_i++;
        });
    }
    h_neighbor_start[2 * size] = adjacency_i;

    /// Move to device memory
    HANDLE_ERROR(cudaMalloc((void**) &adjacency, edges_count * sizeof(nid_t)));
    HANDLE_ERROR(cudaMalloc((void**) &neighbor_start, 2 * size * sizeof(nid_t) + 1));
    HANDLE_ERROR(cudaMemcpy(adjacency, h_adjacency, 
        edges_count * sizeof(nid_t), cudaMemcpyHostToDevice));
    HANDLE_ERROR(cudaMemcpy(neighbor_start, h_neighbor_start, 
        2 * size * sizeof(nid_t) + 1, cudaMemcpyHostToDevice));

#ifndef DEBUG_HOST
    free(h_adjacency);
    free(h_neighbor_start);
#endif /* DEBUG_HOST */
}

void BidirectedGraphGPU::dealloc() {
#ifdef DEBUG_HOST
    free(h_adjacency);
    free(h_neighbor_start);
#endif /* DEBUG_HOST */
    HANDLE_ERROR(cudaFree(neighbor_start));
    HANDLE_ERROR(cudaFree(adjacency));
}