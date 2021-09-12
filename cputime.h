#ifndef __CPU_TIME
#define __CPU_TIME

#if _MSC_VER

#include <time.h>
#if !defined(CLOCKS_PER_SEC) && defined(CLK_TCK)
#define CLOCKS_PER_SEC CLK_TCK
#endif

class CpuTime {
public:
	CpuTime() { init(); }
	void init() { start = cpu_time(); }
	double operator () () const { return cpu_time() - start; }
private:
	double cpu_time() const {
		static clock_t last = static_cast<clock_t>(-1);
		clock_t t = clock();
		if (last == static_cast<clock_t>(-1)) { last = t; }
		return (static_cast<double>(t) - last) / CLOCKS_PER_SEC;
	}
	mutable double start;
};

#else

#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

class CpuTime{
public:
	CpuTime(): start(0) { init(); }
	void init(){ start += (*this)(); }
	double operator () () const {
		struct rusage temp;
		if (getrusage(RUSAGE_SELF, &temp )) return 0.0;
		return (static_cast<double>(temp.ru_utime.tv_sec) 
			+ static_cast<double>(temp.ru_utime.tv_usec)/1000000 
			- start);
	};
private:
	double start;
};

#endif

#endif
