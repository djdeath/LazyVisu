#ifndef __LAZY_PASSTHROUGH_INTERNAL_H__
#define __LAZY_PASSTHROUGH_INTERNAL_H__

#define LAZY_PASSTHROUGH_HOST "localhost"
#define LAZY_PASSTHROUGH_PORT (4242)

typedef char lazy_char_t;

#ifdef __LONG_TYPE_32__
typedef unsigned long lazy_int_t;
#elif defined(__LONG_TYPE_64)
typedef unsigned int lazy_int_t;
#else
# error "Please define __LONG_TYPE_32__ or __LONG_TYPE_64"
#endif

typedef enum
{
        LAZY_ADD_LAYER,
        LAZY_DEL_LAYER,
        LAZY_FLIP_LAYER,
} lazy_operation_t;

typedef struct
{
        lazy_int_t x;
        lazy_int_t y;
        lazy_int_t w;
        lazy_int_t h;
} lazy_rectangle_t;

typedef struct
{
        lazy_operation_t operation;

        lazy_int_t layer_num;

        lazy_int_t width;
        lazy_int_t height;

        lazy_rectangle_t src;
        lazy_rectangle_t dst;

        lazy_char_t filename[256];
} lazy_operation_addlayer_t;

typedef struct
{
        lazy_operation_t operation;

        lazy_int_t layer_num;
} lazy_operation_dellayer_t;

typedef struct
{
        lazy_operation_t operation;

        lazy_int_t layer_num;

        lazy_char_t filename[256];
} lazy_operation_fliplayer_t;

#endif /* __LAZY_PASSTHROUGH_INTERNAL_H__ */
