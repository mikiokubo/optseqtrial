#include <vector>
#include <limits>
using namespace std;

template<typename T, typename U>
class Network {
	using Id = unsigned int;
	static constexpr Id nil = numeric_limits<Id>::max();
public:
	Network(T infinity = numeric_limits<T>::max()) : inf(infinity), positive_cycle(0) {}
	~Network() {
		if (positive_cycle) { delete positive_cycle; }
	}
	Network(const Network&) = delete;
	const Network& operator=(const Network&) = delete;

	// resize() removes all of the arcs
	void resize(U n) {
		vertex.clear(); vertex.resize(n);
		arc.clear();
	}

	void add_arc(U from, U to, T length) {
		auto k = arc.size();
		arc.resize(k + 1);
		arc[k].from = from;
		arc[k].to = to;
		arc[k].length = length;
		arc[k].next_out = vertex[from].outgoing; ++vertex[from].outdegree;
		arc[k].next_in = vertex[to].incoming; ++vertex[to].indegree;
		vertex[from].outgoing = vertex[to].incoming = static_cast<Id>(k);
	}
	U get_outdegree(U i) const { return vertex[i].outdegree; }
	U get_indegree(U i) const { return vertex[i].indegree; }

	bool all_pairs_longest_path();
	T get_dist(U s, U t) const { return dist[s][t]; }
	const U* get_positive_cycle() const { return positive_cycle; }

	U strongly_connected_components();
	U get_strongly_connected_component(U i) const { return scc[i]; }

	T topological_order(vector<T>& score, vector<U>& group);
	U get_topological_order(U i) const { return topological[i]; }

	void closure(vector<U>& nodes, bool outgoing = true);
private:
	// classes
	struct Arc {
		T from = 0, to = 0;
		T length = 0;
		Id next_out = nil, next_in = nil;
	};
	struct Vertex {
		Vertex() {}
		U outdegree = 0, indegree = 0;
		Id outgoing = nil, incoming = nil;
		T label = 0;
		U pred = 0;
	};

	class Queue { // used in label_correcting
	public:
		Queue(size_t n) :
			size(n), array(size + 1), in_queue(size), num_in_queue(size) {
			for (U i = 0; i < size; i++) {
				in_queue[i] = false; num_in_queue[i] = 0;
			}
		}
		~Queue() {}
		bool empty() { return front == rear; }
		bool enqueue(T key) {
			if (in_queue[key]) { return true; }
			in_queue[key] = true;
			if (++num_in_queue[key] > size) { return false; } // exceed max_num
			rear = (rear + 1) % (size + 1); array[rear] = key;
			return true;
		}
		T dequeue() {
			front = (front + 1) % (size + 1);
			in_queue[array[front]] = false;
			return array[front];
		}
	private:
		size_t size;
		vector<T> array;
		vector<bool> in_queue;
		vector<U> num_in_queue;
		U front = 0, rear = 0;
	};


	class Heap { // used in dijkstra
	public:
		//Heap() {}
		Heap(U n, vector<T>& dist) : array(n), location(n), key(dist) {}
		~Heap() {}
		void initialize(U n, vector<T>* dist) {
			array.resize(n);
			location.resize(n);
			key = dist;
		}
		bool empty() const { return size == 0; }
		void insert(U i) {
			array[size] = i; location[i] = size;
			shiftup(size);
			size++;
		}
		U delete_max() {
			U max = array[0];
			array[0] = array[--size]; location[array[0]] = 0;
			shiftdown(0);
			return max;
		}
		void increase_key(U i) { shiftup(location[i]); }
	private:
		vector<U> array;
		vector<U> location;
		U size = 0;
		vector<T>& key;

		void swap(U& a, U& b) {
			auto temp = a; a = b; b = temp;
		}
		void shiftup(U pos) {
			if (pos == 0) return;
			auto pos0 = (pos - 1) / 2;
			if (key[array[pos0]] < key[array[pos]]) {
				swap(array[pos0], array[pos]);
				swap(location[array[pos0]], location[array[pos]]);
				shiftup(pos0);
			}
		}
		inline void shiftdown(U pos) {
			auto pos1 = (pos + 1) * 2 - 1;
			if (pos1 >= size) return;
			if (pos1 + 1 < size && key[array[pos1]] < key[array[static_cast<size_t>(pos1) + 1]]) { pos1++; }
			if (key[array[pos1]] > key[array[pos]]) {
				swap(array[pos1], array[pos]);
				swap(location[array[pos1]], location[array[pos]]);
				shiftdown(pos1);
			}
		}
	};
	//Heap heap;

	// private member function;
	void dijkstra(U);
	void postorder(U, vector<U>&);
	void scc_dfs(U, U);
	void closure_dfs_outgoing(U, vector<U>&, vector<bool>&);
	void closure_dfs_incoming(U, vector<U>&, vector<bool>&);
	// private data members
	const T inf;
	vector<Vertex> vertex;
	vector<Arc> arc;
	vector<vector<T> > dist;
	U* positive_cycle;
	vector<T> scc;
	vector<U> topological;
};


// ========== longest path ==========
template<typename T, typename U>
bool Network<T, U>::all_pairs_longest_path() { // return false, if there exists a positive cycle
	// FIFO label correcting
	auto s = 0;
	positive_cycle = 0;
	for (U i = 0; i < vertex.size(); i++) { vertex[i].label = -inf; }
	vertex[s].label = 0; vertex[s].pred = s;
	Queue queue(vertex.size());
	queue.enqueue(s);
	while (!queue.empty()) {
		auto i = queue.dequeue();
		for (Id a = vertex[i].outgoing; a != nil; a = arc[a].next_out) {
			U j = arc[a].to;
			if (vertex[j].label < vertex[i].label + arc[a].length) {
				vertex[j].label = vertex[i].label + arc[a].length;
				vertex[j].pred = i;
				if (!queue.enqueue(j)) { // positive cycle detected

					for (U jj = 0; jj < vertex.size(); ++jj) {
						cout << j << " ";
						j = vertex[j].pred;
					}
					cout << endl;

					return false;
				}
			}
		}
	}
	//for (unsigned int i = 0; i < vertex.size(); i++) cout << i << " " << vertex[i].label << endl;


	// Dijkstra algorithm
	dist.clear(); dist.resize(vertex.size());
	dist[0].resize(vertex.size());
	for (U i = 0; i < vertex.size(); i++) { dist[0][i] = vertex[i].label; }
	for (U i = 1; i < vertex.size(); i++) { 	dijkstra(i); }
	return true;
}

// ========== dijkstra ==========
template<typename T, typename U>
void Network<T, U>::dijkstra(U s) { // dist[s][.] will be calculated
	dist[s].resize(vertex.size(), -inf);
	dist[s][s] = 0;
	Heap heap(static_cast<U>(vertex.size()), dist[s]);
	//heap.initialize(static_cast<U>(vertex.size()), &dist[s]);
	heap.insert(s);

	//cout << "#" << s << endl;

	while (!heap.empty()) {
		U i = heap.delete_max();
		for (Id a = vertex[i].outgoing; a != nil; a = arc[a].next_out) {
			U j = arc[a].to;
			T value = arc[a].length + vertex[i].label - vertex[j].label + dist[s][i];
			if (dist[s][j] < value) {
				if (dist[s][j] == -inf) { dist[s][j] = value; heap.insert(j); }
				else { dist[s][j] = value; heap.increase_key(j); }
			}
		}
	}
	for (U i = 0; i < vertex.size(); i++) {
		if (dist[s][i] != -inf) { dist[s][i] += vertex[i].label - vertex[s].label; }
	}
}

// ========== strongly connected components ==========
template<typename T, typename U>
U Network<T, U>::strongly_connected_components() { // return the number of strongly connected components
	vector<U> vertex_list;
	scc.clear(); 
	scc.resize(vertex.size(), 0);
	for (U i = 0; i < vertex.size(); i++) {
		if (scc[i] == 0) { postorder(i, vertex_list); }
	}
	U num_scc = 0;
	for (U ii = 0; ii < vertex.size(); ii++) {
		U i = vertex_list[vertex.size() - ii - 1];
		if (scc[i] == -1) { scc_dfs(i, num_scc++); }
	}
	return num_scc;
}

template<typename T, typename U>
void Network<T, U>::postorder(U i, vector<U>& vertex_list) {
	// scc[.] is set to -1 when the vertex is visited, 
	//	and will be set to its strongly connected component number in scc_dfs;
	scc[i] = -1;
	for (Id a = vertex[i].outgoing; a != nil; a = arc[a].next_out) {
		U i2 = arc[a].to;
		if (scc[i2] == 0) { postorder(i2, vertex_list); }
	}
	vertex_list.push_back(i);
}

template<typename T, typename U>
void Network<T, U>::scc_dfs(U i, U scc_id) {
	scc[i] = scc_id;
	for (Id a = vertex[i].incoming; a != nil; a = arc[a].next_in) {
		U i2 = arc[a].from;
		if (scc[i2] == -1) { scc_dfs(i2, scc_id); }
	}
}

// ========== topological order ==========
template<typename T, typename U>
T Network<T, U>::topological_order(vector<T>& score, vector<U>& group) {
	// calculate topological order; vertices in the same group must be consecutively ordered
	// return 1, if the digraph is cyclic
	topological.resize(vertex.size());
	vector<U> num_pred(vertex.size(), 0);
	for (U a = 0; a < arc.size(); a++) { num_pred[arc[a].to]++; }

	Heap heap(static_cast<U>(vertex.size()), score);
	vector<U> spool;
	for (U i = 0; i < vertex.size(); i++) {
		if (num_pred[i] == 0) { spool.push_back(i); }
	}
	for (U k = 0; k < vertex.size(); k++) {
		if (heap.empty()) {
			if (spool.empty()) { return 1; }
			// arrange spool so spool[0] is an element with the highest score
			for (U ii = 1; ii < spool.size(); ii++) {
				if (score[spool[ii]] > score[spool[0]]) { swap(spool[0], spool[ii]); }
			}
			U next_group = group[spool[0]];
			U ii = 0;
			while (ii < spool.size()) {
				if (group[spool[ii]] == next_group) {
					heap.insert(spool[ii]);
					spool[ii] = spool[spool.size() - 1]; spool.pop_back();
				}
				else { ii++; }
			}
		}
		U i = heap.delete_max();
		topological[k] = i;
		for (Id a = vertex[i].outgoing; a != nil; a = arc[a].next_out) {
			U j = arc[a].to;
			if (--num_pred[j] == 0) {
				if (group[j] == group[i]) { heap.insert(j); }
				else { spool.push_back(j); }
			}
		}
	}
	return 0;
}

// ========== closure ==========
template<typename T, typename U>
void Network<T, U>::closure(vector<U>& nodes, bool outgoing) {
	vector<bool> flag(vertex.size(), false);
	for (U k = 0; k < nodes.size(); ++k) {
		if (!flag[nodes[k]]) {
			flag[nodes[k]] = true;
			if (outgoing) { closure_dfs_outgoing(nodes[k], nodes, flag); }
			else { closure_dfs_incoming(nodes[k], nodes, flag); }
		}
	}
}

template<typename T, typename U>
void Network<T, U>::closure_dfs_outgoing(U i, vector<U>& nodes, vector<bool>& flag) {
	for (Id a = vertex[i].outgoing; a != nil; a = arc[a].next_out) {
		U i2 = arc[a].to;
		if (!flag[i2]) {
			nodes.push_back(i2);
			flag[i2] = true;
			closure_dfs_outgoing(i2, nodes, flag);
		}
	}
}
template<typename T, typename U>
void Network<T, U>::closure_dfs_incoming(U i, vector<U>& nodes, vector<bool>& flag) {
	for (Id a = vertex[i].incoming; a != nil; a = arc[a].next_in) {
		U i2 = arc[a].from;
		if (!flag[i2]) {
			nodes.push_back(i2);
			flag[i2] = true;
			closure_dfs_incoming(i2, nodes, flag);
		}
	}
}
