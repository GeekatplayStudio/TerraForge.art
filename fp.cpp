#include <cstdio>
double mul_add(double a,double b,double c){return a*b+c;}
int main(){double r=mul_add(0.1,0.2,-0.02);unsigned long long b;__builtin_memcpy(&b,&r,8);
printf("%.20e %016llx FMA=%d\n",r,b,
#ifdef __FP_FAST_FMA
1
#else
0
#endif
);return 0;}
