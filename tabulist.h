#include <climits>
#include <vector>
#include <iostream>

using namespace std;

// ==================== Tabulist ====================
template<typename T, typename U>
class Tabulist{
	class Attribute{
	public:
		Attribute(){};
		T counter = INT_MIN;
		T f = 0, diff = 0;
		U num_attr_used = 0;
		T ltm = 0;
	};
public:
	Tabulist(){}
	void init(U n, U t=1){
		num_attr = n;
		tenure = t;
		attr.resize(n);
	}
	void clear(U new_tenure = 0){
		if (new_tenure > 0) tenure = new_tenure;
		counter += tenure*2;
		last_clear = counter;
		num_attr_used = 0;
	}
	bool in_list(U attribute) const { return counter <= attr[attribute].counter + tenure; }
	bool operator()(U attribute, T f, T diff) const {
		return  in_list(attribute) && !(f < attr[attribute].f&& attr[attribute].diff <= 0 && diff < 0);
	}
	void update(U attribute, U reverse_attribute, T f, T diff, T perturbation, bool print);
	T get_tenure() const { return tenure; }
	T get_ltm(U attribute) const { return attr[attribute].ltm; }
private:
	T counter = 0;
	U num_attr = 0;
	T tenure = 0;
	T max_tenure = 0, sum_tenure = 0;
	T last_clear = 0;
	U num_attr_used = 0;
	vector<Attribute> attr;
};

// ========== update ==========
template<typename T, typename U>
void Tabulist<T, U>::update(U attribute, U reverse_attribute, T f, T diff, T perturbation, bool print) {
	if (attr[attribute].counter <= last_clear) ++num_attr_used;

	T diff_tenure = 0;
	if (print) {
		cout << ": t=" << tenure << ", <" << attribute << "><" << reverse_attribute << ">(" << num_attr_used << "/" << num_attr << ") ";
		cout << attr[attribute].counter << " " << counter;
	}

	// all attributes are used 
	if (num_attr_used >= num_attr) {
		clear(1);
		if (print) { cout << "[reset all]"; }
	}

	// aspiration
	if (in_list(reverse_attribute)) {
		--diff_tenure;
		if (print) { cout << "[aspiration]"; }
	}
	else {
		// cycling occurs
		if (attr[attribute].counter > last_clear && attr[attribute].num_attr_used == num_attr_used) {
			++diff_tenure;
			if (print) { cout << "[increase]"; }
			//			clear(tenure);
			last_clear = counter;
			num_attr_used = 0;
		}
	}
	tenure = max<U>(1, tenure + diff_tenure);
	attr[attribute].counter = counter + perturbation;
	attr[attribute].f = f;
	attr[attribute].diff = diff;
	attr[attribute].num_attr_used = num_attr_used;
	++attr[attribute].ltm;
	++counter;
	if (tenure > max_tenure) { max_tenure = tenure; }
	sum_tenure += tenure;
}
