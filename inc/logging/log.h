//
// Created by ikryxxdev on 2/10/26.
//

#ifndef IKCAS_LOG_H
#define IKCAS_LOG_H

#define NOT_IMPLEMENTED()                                                      \
    do {                                                                       \
        printf("%s:%d: Not implemented\n", __FILE__, __LINE__);                \
        exit(0);                                                               \
    } while (0)
#define UNREACHABLE()                                                          \
    do {                                                                       \
        printf("%s:%d: Unreachable\n", __FILE__, __LINE__);                    \
        exit(0);                                                               \
    } while (0)

#endif // IKCAS_LOG_H
