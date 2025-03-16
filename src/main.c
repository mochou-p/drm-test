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

/*************************************************************************************************/

struct square {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
    uint32_t color;
    uint32_t border;
    uint32_t thickness;
};

/*************************************************************************************************/

static unsigned char *wallpaper;
static long          pixel_count;

/*************************************************************************************************/

int load_ppm(const char *filepath) {
    int exitcode = EXIT_FAILURE;

    // open file //////////////////////////////////////////////////////////////////////////////////

    FILE *file = fopen(filepath, "r");
    if (NULL == file) {
        perror("fopen");
        goto end;
    }

    // parse version //////////////////////////////////////////////////////////////////////////////

    char version[3];
    if (NULL == fgets(version, 3, file)) {
        perror("fgets version");
        goto close;
    }
    if (strcmp(version, "P6")) {
        printf("bad PPM version\n");
        goto close;
    }

    // parse dimensions ///////////////////////////////////////////////////////////////////////////

    long width, height;
    if (2 != fscanf(file, "%ld %ld\n", &width, &height)) {
        perror("fscanf width height");
        goto close;
    }
    if (width == 0 || height == 0) {
        printf("bad PPM dimensions\n");
        goto close;
    }
    pixel_count = width * height;

    // parse max //////////////////////////////////////////////////////////////////////////////////

    long max;
    if (1 != fscanf(file, "%ld\n", &max)) {
        perror("fscanf max value");
        goto close;
    }
    if (255 != max) {
        printf("bad PPM max\n");
        goto close;
    }

    // count pixel bytes //////////////////////////////////////////////////////////////////////////

    long start  = ftell(file);
    fseek(file, 0, SEEK_END);
    long end    = ftell(file);
    long amount = end - start;
    long values = pixel_count * 3;
    if (values != amount) {
        printf("bad PPM bytes (%ld!=%ld)\n", values, amount);
        goto close;
    }

    // alloc and read /////////////////////////////////////////////////////////////////////////////

    fseek(file, start, SEEK_SET);
    long bytes = amount * sizeof(char);
    wallpaper  = malloc(bytes);
    if (NULL == wallpaper) {
        perror("malloc");
        goto close;
    }
    if (0 == fread(wallpaper, sizeof(char), amount, file)) {
        perror("fread");
        goto close;
    }

    // cleanup ////////////////////////////////////////////////////////////////////////////////////

    exitcode = EXIT_SUCCESS;
close:
    fclose(file);
end:
    return exitcode;
}

/*************************************************************************************************/

int main(int argc, char **argv) {
    int exitcode = EXIT_FAILURE;

    // validate args //////////////////////////////////////////////////////////////////////////////

    if (2 != argc) {
        printf("usage: drm_test <PATH_TO_PPM_IMAGE>\n");
        goto end;
    }
    const char *wallpaper_path = argv[1];
    if (!access(wallpaper_path, F_OK)) {
        printf("%s does not exist\n", wallpaper_path);
        goto end;
    }
    if (!access(wallpaper_path, R_OK)) {
        printf("%s cannot be read\n", wallpaper_path);
        goto end;
    }

    // open device ////////////////////////////////////////////////////////////////////////////////

    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) {
        fd = open("/dev/dri/card1", O_RDWR);
        if (fd < 0) {
            perror("open card0/card1");
            goto end;
        }
    }

    // permissions ////////////////////////////////////////////////////////////////////////////////

    if (drmSetMaster(fd) < 0) {
        printf("you need to run this from tty\n");
        goto close;
    }

    // get resources //////////////////////////////////////////////////////////////////////////////

    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) {
        perror("drmModeGetResources");
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
    size_t   bytes  = width * height * 4;

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

    // load wallpaper /////////////////////////////////////////////////////////////////////////////

    if (0 != load_ppm(wallpaper_path+3)) { // FIXME: temp +3 to ignore "../" from `make debug`
        printf("failed to load image\n");
        goto unmap_buf;
    }

    // draw ///////////////////////////////////////////////////////////////////////////////////////

    const struct square squares[2] = {
        {100, 100, 400, 200, 0xFF000000, 0xFFFFFFFF, 20},
        {150,  50, 175, 600, 0xFFFF0000, 0xFFFFFF00, 10}
    };
    const uint32_t n_squares = sizeof(squares) / sizeof(struct square);

    uint32_t color, i, x, y;
    uint32_t *framebuffer = (uint32_t *) buffer;
    uint32_t n_pixels     = width * height;
    for (i = 0; i < n_pixels; ++i) {
        x = i % width;
        y = i / width;

        // wallpaper
        color = 0xFF000000             // A
            + (wallpaper[i*3  ] << 16) // R
            + (wallpaper[i*3+1] <<  8) // G
            +  wallpaper[i*3+2];       // B

        // squares
        for (uint32_t s = 0; s < n_squares; ++s) {
            if (
                x >= squares[s].left   &&
                x <= squares[s].right  &&
                y >= squares[s].top    &&
                y <= squares[s].bottom
            ) {
                if (
                    x >= squares[s].left   + squares[s].thickness &&
                    x <= squares[s].right  - squares[s].thickness &&
                    y >= squares[s].top    + squares[s].thickness &&
                    y <= squares[s].bottom - squares[s].thickness
                ) {
                    color = squares[s].border;
                } else {
                    color = squares[s].color;
                }
                break;
            }
        }

        framebuffer[i] = color;
    }

    // present ////////////////////////////////////////////////////////////////////////////////////

    if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &connector->connector_id, 1, &mode)) {
        perror("drmModeSetCrtc");
        goto free_wall;
    }
    sleep(3);

    // cleanup ////////////////////////////////////////////////////////////////////////////////////

    // FIXME: properly free unmapped framebuffer?

    exitcode = EXIT_SUCCESS;
free_wall:
    free(wallpaper);
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

