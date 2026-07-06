/* macOS/Linux host shim: Turbo C++'s <malloc.h> declared malloc/calloc/realloc/free,
 * which <stdlib.h> already provides on a standard host. */
#include <stdlib.h>
