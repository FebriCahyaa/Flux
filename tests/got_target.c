/* Test fixture: a shared object that calls an imported function through its GOT. */
extern int flux_import(int);
int flux_call_import(int x) { return flux_import(x) + 1000; }
