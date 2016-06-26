#ifndef __MUX_DEFINE_H
#define __MUX_DEFINE_H

#define MAX_SEQUENCE 

#define PEP_TRACE(fmt, args...)// fprintf(stdout, fmt"\n", ##args)
#define PEP_INFO(fmt, args...) fprintf(stdout, fmt"\n", ##args)
#define PEP_WARN(fmt, args...) fprintf(stderr, fmt"\n", ##args)
#define PEP_ERROR(fmt, args...) fprintf(stderr, fmt"\n", ##args)

#endif
