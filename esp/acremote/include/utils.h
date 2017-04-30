#ifndef _UTILS_H
#define _UTILS_H

#define MIN(X,Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X,Y) (((X) > (Y)) ? (X) : (Y))
#define CLAMP(x,a,b) (MAX((a), MIN((x), (b))))

/* statically allocated arrays only */
#define N_ELEMENTS(arr) (sizeof (arr) / sizeof ((arr)[0]))

#endif /* _UTILS_H */
