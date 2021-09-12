//////////////////////////////////////////////////////////////////////
// Reference:																													
//	 Summit, S., ``C programming FAQ's Frequently Asked Questions'', 
//	 Addison-Wesley Publishing Company, Inc., 1996									 
//
// Random:
//		init(int);
//		operator() (int,int);
//		memory();
//		resume();
//////////////////////////////////////////////////////////////////////
#ifndef __RANDOM
#define __RANDOM

template<typename T>
class Random{
public:
	Random(){ init(1); }
	~Random(){}
	T init(T i) {
		seed = (i) ? static_cast<long int>(i) : 1; 
		return i;
	}
	T operator () (T min, T max) {
		if (max < min) { max = min; }
		long int hi = seed / q;
		long int lo = seed % q;
		long int test = a * lo - r * hi;
		if (test > 0) { seed = test; }
		else { seed = test + m; }
		return (min + seed / ((m - 1) / (static_cast<int>(max) - static_cast<int>(min) + 1) + 1));
	}
	void memory(){ stack = seed; }
	void resume(){ seed = stack; }
private:
	enum { a = 48271, m = 2147483647, q	= m/a, r = m%a };
	long int seed = 1;
	long int stack = 0;
};
#endif
