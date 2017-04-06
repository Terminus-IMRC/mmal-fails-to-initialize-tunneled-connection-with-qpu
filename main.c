/*
 * Copyright (c) 2017 Sugizaki Yukimasa (ysugi@idein.jp)
 * All rights reserved.
 *
 * This software is licensed under a Modified (3-Clause) BSD License.
 * You should have received a copy of this license along with this
 * software. If not, contact the copyright holder above.
 */

/*
 * MMAL connection example is here:
 * https://github.com/raspberrypi/userland/blob/master/interface/mmal/test/examples/example_connections.c
 */

#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <interface/mmal/util/mmal_connection.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "./mailbox.h"


/*
 * If QPU_ENABLE=1 and flag=tunnelling and this program is ran as the first camera user, the camera stack hungs.
 */
#define QPU_ENABLE 1
#if 1
#    define CONNECTION_FLAG MMAL_CONNECTION_FLAG_TUNNELLING
#else
#    define CONNECTION_FLAG 0
#endif


/* These parameters doesn't matter as far as I know. */
#define ENCODING MMAL_ENCODING_RGBA
#define WIDTH  1024
#define HEIGHT 768
#define ZERO_COPY 1
#define DEV_MEM "/dev/mem"


struct cb_context {
    MMAL_STATUS_T status;
    VCOS_SEMAPHORE_T sem;
};


#define _assert(x) \
    do { \
        int ret = (int) (x); \
        if (!ret) { \
            fprintf(stderr, "%s:%d: Assertation failed: %s\n", __FILE__, __LINE__, #x); \
            exit(EXIT_FAILURE); \
        }\
    } while (0)

#define _check_vcos(x) \
    do { \
        VCOS_STATUS_T status = (x); \
        if (status != VCOS_SUCCESS) { \
            fprintf(stderr, "%s:%d: VCOS call failed: 0x%08x\n", __FILE__, __LINE__, status); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)


#define _check_mmal(x) \
    do { \
        MMAL_STATUS_T status = (x); \
        if (status != MMAL_SUCCESS) { \
            fprintf(stderr, "%s:%d: MMAL call failed: 0x%08x\n", __FILE__, __LINE__, status); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define LOG(fmt, ...) \
    do { \
        fprintf(stderr, "%s:%d:%s: ", __FILE__, __LINE__, __func__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } while (0)


static MMAL_STATUS_T config_port(MMAL_PORT_T *port, const MMAL_FOURCC_T encoding, const int width, const int height)
{
    port->format->encoding = encoding;
    port->format->es->video.width  = VCOS_ALIGN_UP(width,  32);
    port->format->es->video.height = VCOS_ALIGN_UP(height, 16);
    port->format->es->video.crop.x = 0;
    port->format->es->video.crop.y = 0;
    port->format->es->video.crop.width  = width;
    port->format->es->video.crop.height = height;
    return mmal_port_format_commit(port);
}

static void control_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    LOG("Called");

    MMAL_PARAM_UNUSED(port);
    mmal_buffer_header_release(buffer);
}

static void connection_cb(MMAL_CONNECTION_T *conn)
{
    struct cb_context *ctx = (struct cb_context*) conn->user_data;

    LOG("Called");

    _check_vcos(vcos_semaphore_post(&ctx->sem));
}

void do_mmal()
{
    int i;
    MMAL_COMPONENT_T *cp_source = NULL, *cp_render = NULL;
    MMAL_CONNECTION_T *conn = NULL;
    struct cb_context ctx;

    _check_mmal(mmal_component_create("vc.ril.camera", &cp_source));
    {
        MMAL_PORT_T *control = cp_source->control;
        _check_mmal(mmal_port_parameter_set_int32(control, MMAL_PARAMETER_CAMERA_NUM, 0));
        _check_mmal(mmal_port_enable(control, control_cb));
    }
    {
        MMAL_PORT_T *output = cp_source->output[0];

        _check_mmal(config_port(output, ENCODING, WIDTH, HEIGHT));
        _check_mmal(mmal_port_parameter_set_boolean(output, MMAL_PARAMETER_ZERO_COPY, ZERO_COPY));
    }
    _check_mmal(mmal_component_enable(cp_source));

    _check_mmal(mmal_component_create("vc.ril.video_render", &cp_render));
    {
        MMAL_PORT_T *input = cp_render->input[0];

        _check_mmal(config_port(input, ENCODING, WIDTH, HEIGHT));
        _check_mmal(mmal_port_parameter_set_boolean(input, MMAL_PARAMETER_ZERO_COPY, ZERO_COPY));
    }
    _check_mmal(mmal_component_enable(cp_source));

    _check_mmal(mmal_connection_create(&conn, cp_source->output[0], cp_render->input[0], CONNECTION_FLAG));

    ctx.status = MMAL_SUCCESS;
    _check_vcos(vcos_semaphore_create(&ctx.sem, "example", 1));
    conn->user_data = (void*) &ctx;
    conn->callback = connection_cb;
    _check_mmal(mmal_connection_enable(conn));

    /* For non-tunnelling connection: */
    for (i = 0; i < 30; i ++) {
        MMAL_BUFFER_HEADER_T *buffer = NULL;

        vcos_semaphore_wait(&ctx.sem);

        _check_mmal(ctx.status);

        if (conn->flags & MMAL_CONNECTION_FLAG_TUNNELLING)
            continue;

        while ((buffer = mmal_queue_get(conn->pool->queue)) != NULL) {
            _check_mmal(mmal_port_send_buffer(conn->out, buffer));
        }
        while ((buffer = mmal_queue_get(conn->queue)) != NULL) {
            _check_mmal(mmal_port_send_buffer(conn->in, buffer));
        }
    }

    _check_mmal(mmal_connection_disable(conn));
    _check_mmal(mmal_connection_destroy(conn));
    _check_mmal(mmal_component_destroy(cp_render));
    _check_mmal(mmal_component_destroy(cp_source));
    vcos_semaphore_delete(&ctx.sem);
}

int main()
{
    int fd_mb;

    fd_mb = mailbox_open();
    mailbox_qpu_enable(fd_mb, QPU_ENABLE);
    do_mmal();
    if (QPU_ENABLE)
        mailbox_qpu_enable(fd_mb, 0);
    mailbox_close(fd_mb);

    return 0;
}
