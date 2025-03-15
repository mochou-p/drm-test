// drm-test/src/main.c

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>


int main(void) {
    int exitcode = EXIT_FAILURE;

    // open device ////////////////////////////////////////////////////////////////////////////////

    int fd = open("/dev/dri/card1", O_RDWR);
    if (fd < 0) {
        perror("open card0");
        goto end;
    }

    // permissions ////////////////////////////////////////////////////////////////////////////////

    if (drmSetMaster(fd) < 0) {
        perror("drmSetMaster");
        goto close;
    }

    // get resources //////////////////////////////////////////////////////////////////////////////

    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) {
        perror("drmModeGetResources\n\n\n");
        goto deop;
    }

    // find connector /////////////////////////////////////////////////////////////////////////////

    drmModeConnector *connector       = NULL;
    bool              found_connector = false;
    uint32_t          crtc_id;
    for (int i = 0; i < resources->count_connectors; ++i) {
       connector = drmModeGetConnector(fd, resources->connectors[i]);

       if (!connector) {
           perror("drmModeGetConnector");
           goto free_res;
       }

       if (DRM_MODE_CONNECTED != connector->connection || !connector->encoder_id) {
           drmModeFreeConnector(connector);
           continue;
       }

       drmModeEncoderPtr encoder = drmModeGetEncoder(fd, connector->encoder_id);
       if (!encoder) {
           drmModeFreeConnector(connector);
           continue;
       }

       crtc_id = encoder->crtc_id;
       drmModeFreeEncoder(encoder);

       found_connector = true;
       break;
    }

    if (!found_connector) {
        printf("did not find a connected connector");
        goto free_con;
    }

    // set mode ///////////////////////////////////////////////////////////////////////////////////

    if (0 == connector->count_modes) {
        printf("no available modes\n");
        goto free_con;
    }

    drmModeModeInfo mode = connector->modes[0];

    // allocate buffer ////////////////////////////////////////////////////////////////////////////

    uint32_t width  = mode.hdisplay;
    uint32_t height = mode.vdisplay;
    size_t   bytes  = width * height * 4; // 32 BPP: 4B/pixel

    void *buffer = malloc(bytes);
    if (!buffer) {
        perror("malloc for buffer");
        goto free_con;
    }
    bool mapped = false;

    // map buffer /////////////////////////////////////////////////////////////////////////////////

    struct drm_mode_create_dumb create_dumb;
    memset(&create_dumb, 0, sizeof(create_dumb));
    create_dumb.width  = width;
    create_dumb.height = height;
    create_dumb.bpp    = 32;

    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb)) {
        perror("ioctl create dumb");
        goto free_buf;
    }

    struct drm_mode_map_dumb map_dumb;
    memset(&map_dumb, 0, sizeof(map_dumb));
    map_dumb.handle = create_dumb.handle;

    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb)) {
        perror("ioctl map dumb");
        goto free_buf;
    }

    buffer = mmap(0, create_dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_dumb.offset);
    if (MAP_FAILED == buffer) {
        perror("mmap");
        goto free_buf;
    }
    mapped = true;

    // create framebuffer /////////////////////////////////////////////////////////////////////////

    uint32_t fb;
    if (drmModeAddFB(fd, width, height, 24, 32, create_dumb.pitch, create_dumb.handle, &fb) != 0) {
        perror("drmModeAddFB");
        goto unmap_buf;
    }

    // draw ///////////////////////////////////////////////////////////////////////////////////////

    uint32_t color = 0xFF8C53DB;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t i = y * width + x;

            ((uint32_t *) buffer)[i] = color;
        }
    }

    // present ////////////////////////////////////////////////////////////////////////////////////

    if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &connector->connector_id, 1, &mode)) {
        perror("drmModeSetCrtc");
        goto unmap_buf;
    }

    sleep(1);

    // cleanup ////////////////////////////////////////////////////////////////////////////////////

    printf("\x1b[32msuccess\x1b[0m\n");

    exitcode = EXIT_SUCCESS;
unmap_buf:
    munmap(buffer, create_dumb.size);
free_buf:
    if (!mapped) {
        free(buffer);
    }
free_con:
    if (NULL != connector) {
        drmModeFreeConnector(connector);
    }
free_res:
    drmModeFreeResources(resources);
deop:
    drmDropMaster(fd);
close:
    close(fd);
end:
    return exitcode;
}

