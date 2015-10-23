#include "class_defs/sysinfo.h"
#include "class_defs/matrix.h"
#include "class_defs/states.h"
#include "include/ckcomplex.h"
#include "/sw/include/fftw3.h"
#include "util.h"
#include "my_fftw.h"

void update_Pmtrx(complex *, double, complex *, double, int, int, CMATRIX*);
void modify_state_Uproc(complex *, int [3], int [3], SYSINFO);