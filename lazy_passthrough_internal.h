#ifndef __LAZY_PASSTHROUGH_INTERNAL_H__
#define __LAZY_PASSTHROUGH_INTERNAL_H__

#define LAZY_PASSTHROUGH_HOST "localhost"
#define LAZY_PASSTHROUGH_PORT (4242)

#define LAZY_FILENAME_MAX_LENGHT (10)

typedef char lazy_char_t;

#ifdef __LONG_TYPE_32__
typedef unsigned long lazy_uint_t;
#elif defined(__LONG_TYPE_64)
typedef unsigned int lazy_uint_t;
#else
# error "Please define __LONG_TYPE_32__ or __LONG_TYPE_64"
#endif

/**/
typedef enum
{
        LAZY_OPERATION_ADD_LAYER,
        LAZY_OPERATION_DEL_LAYER,
        LAZY_OPERATION_FLIP_LAYER,
        LAZY_OPERATION_ADD_BUFFER,
        LAZY_OPERATION_DEL_BUFFER,
} lazy_operation_t;

/**/
typedef enum
{
        LAZY_OPERATION_RESULT_SUCCESS,
        LAZY_OPERATION_RESULT_FAILURE,
} lazy_operation_result_t;

/* Add layer */
typedef struct
{
        lazy_uint_t x;
        lazy_uint_t y;
        lazy_uint_t w;
        lazy_uint_t h;
} lazy_rectangle_t;

typedef struct
{
        lazy_operation_t operation;

        lazy_uint_t layer_id;

        lazy_uint_t width;
        lazy_uint_t height;

        lazy_rectangle_t src;
        lazy_rectangle_t dst;

        lazy_uint_t buffer_id;
} lazy_operation_addlayer_t;

typedef struct
{
        lazy_operation_result_t result;
} lazy_operation_addlayer_res_t;

/* Del layer */
typedef struct
{
        lazy_operation_t operation;

        lazy_uint_t layer_id;
} lazy_operation_dellayer_t;

typedef struct
{
        lazy_operation_result_t result;
} lazy_operation_dellayer_res_t;

/* Flip layer */
typedef struct
{
        lazy_operation_t operation;

        lazy_uint_t layer_id;

        lazy_uint_t buffer_id;
} lazy_operation_fliplayer_t;

typedef struct
{
        lazy_operation_result_t result;
} lazy_operation_fliplayer_res_t;

/* New buffer */
typedef struct
{
        lazy_operation_t operation;

        lazy_uint_t width;
        lazy_uint_t height;
        lazy_uint_t bpp;
} lazy_operation_addbuffer_t;

typedef struct
{
        lazy_operation_result_t result;

        lazy_uint_t buffer_id;
} lazy_operation_addbuffer_res_t;

/* Delete buffer */
typedef struct
{
        lazy_operation_t operation;

        lazy_uint_t buffer_id;
} lazy_operation_delbuffer_t;

typedef struct
{
        lazy_operation_result_t result;
} lazy_operation_delbuffer_res_t;

#endif /* __LAZY_PASSTHROUGH_INTERNAL_H__ */
