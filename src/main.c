// drm-test/src/main.c

#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

/*************************************************************************************************/

#define ASCII_NUMBER_OFFSET (48)

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

unsigned char *load_ppm(const char *, uint32_t *, uint32_t *);

// void *mouse_handler(void *);

/*************************************************************************************************/

// static atomic_bool exiting = false;

/*************************************************************************************************/

int main(int argc, char **argv) {
    int exitcode = EXIT_FAILURE;

    /*
    printf("%d\n", atomic_load(&exiting));
    pthread_t mouse_thread;
    pthread_create(&mouse_thread, NULL, mouse_handler, NULL);
    pthread_join(mouse_thread, NULL);
    printf("%d\n", atomic_load(&exiting));
    */

    // validate args //////////////////////////////////////////////////////////////////////////////

    if (2 != argc) {
        printf("usage: drm_test <PATH_TO_PPM_IMAGE>\n");
        goto end;
    }
    const char *wallpaper_path = argv[1];
    if (0 == access(wallpaper_path, F_OK)) {
        printf("%s does not exist\n", wallpaper_path);
        goto end;
    }
    if (0 == access(wallpaper_path, R_OK)) {
        printf("%s cannot be read\n", wallpaper_path);
        goto end;
    }

    // open device ////////////////////////////////////////////////////////////////////////////////

    char          gpu[]     = "/dev/dri/cardX";
    unsigned long last_char = strlen(gpu) - 1;
    int           fd        = -1;
    for (char i = 0; i < 8; ++i) {
        gpu[last_char] = i + ASCII_NUMBER_OFFSET;
        fd = open(gpu, O_RDWR); // | O_NONBLOCK
        if (0 <= fd) {
            break;
        }
    }
    if (0 > fd) {
        printf("could not open /dev/dri/cardX (0..7)\n");
        goto end;
    }

    // permissions ////////////////////////////////////////////////////////////////////////////////

    if (0 > drmSetMaster(fd)) {
        printf("you need to run this from tty\n");
        goto close;
    }

    // get resources //////////////////////////////////////////////////////////////////////////////

    drmModeRes *resources = drmModeGetResources(fd);
    if (NULL == resources) {
        perror("drmModeGetResources");
        goto deop;
    }

    // find connector /////////////////////////////////////////////////////////////////////////////

    drmModeConnector *connector       = NULL;
    bool              found_connector = false;
    uint32_t          crtc_id;
    for (int i = 0; i < resources->count_connectors; ++i) {
       connector = drmModeGetConnector(fd, resources->connectors[i]);

       if (NULL == connector) {
           perror("drmModeGetConnector");
           goto free_res;
       }

       if (DRM_MODE_CONNECTED != connector->connection || !connector->encoder_id) {
           drmModeFreeConnector(connector);
           continue;
       }

       drmModeEncoderPtr encoder = drmModeGetEncoder(fd, connector->encoder_id);
       if (NULL == encoder) {
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

    uint32_t width  = mode.hdisplay;
    uint32_t height = mode.vdisplay;

    // map buffer /////////////////////////////////////////////////////////////////////////////////

    struct drm_mode_create_dumb c_dumb;
    memset(&c_dumb, 0, sizeof(c_dumb));
    c_dumb.width  = width;
    c_dumb.height = height;
    c_dumb.bpp    = 32;

    if (0 != ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c_dumb)) {
        perror("ioctl create dumb");
        goto free_con;
    }

    struct drm_mode_map_dumb map_dumb;
    memset(&map_dumb, 0, sizeof(map_dumb));
    map_dumb.handle = c_dumb.handle;

    if (0 != ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb)) {
        perror("ioctl map dumb");
        goto free_con;
    }

    long offset = (long) map_dumb.offset;
    void *buffer = mmap(0, c_dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
    if (MAP_FAILED == buffer) {
        perror("mmap");
        goto free_con;
    }

    // create framebuffer /////////////////////////////////////////////////////////////////////////

    uint32_t fb;
    if (0 != drmModeAddFB(fd, width, height, 24, 32, c_dumb.pitch, c_dumb.handle, &fb)) {
        perror("drmModeAddFB");
        goto unmap_buf;
    }

    // load wallpaper /////////////////////////////////////////////////////////////////////////////

    // FIXME: temp offset on `wallpaper_path` to ignore "../" from `make debug`
    uint32_t      wallpaper_width, wallpaper_height;
    unsigned char *wallpaper = load_ppm(wallpaper_path + 3, &wallpaper_width, &wallpaper_height);
    if (NULL == wallpaper) {
        printf("failed to load image\n");
        goto unmap_buf;
    }
    uint32_t pixel_count = width * height;
    // TODO: validate dimensions ASAP, not deep in here
    if (pixel_count != wallpaper_width * wallpaper_height) {
        printf("wallpaper resolution does not match your display\n");
        goto free_wall;
    }

    // draw ///////////////////////////////////////////////////////////////////////////////////////

    // colors are ARGB
    const struct square squares[] = {
        {100, 100, 400, 200, 0xFF000000, 0xFFFFFFFF, 20},
        {150,  50, 175, 600, 0xFFFF0000, 0xFFFFFF00, 10}
    };
    const uint32_t n_squares = sizeof(squares) / sizeof(struct square);

    uint32_t color, i, x, y;
    uint32_t *framebuffer = (uint32_t *) buffer;
    for (i = 0; i < pixel_count; ++i) {
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

    if (0 != drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &connector->connector_id, 1, &mode)) {
        perror("drmModeSetCrtc");
        goto free_wall;
    }
    sleep(3);

    // cleanup ////////////////////////////////////////////////////////////////////////////////////

    exitcode = EXIT_SUCCESS;
free_wall:
    free(wallpaper);
unmap_buf:
    munmap(buffer, c_dumb.size);
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

/*************************************************************************************************/

/*
void *mouse_handler(void *arg) {
    (void) arg;

    // open mice //////////////////////////////////////////////////////////////////////////////////

    int fd = open("/dev/input/mice", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/input/mice");
        goto end;
    }

    bool    fix_mouse[3] = {0,0,0};
    uint8_t      data[3] = {0,0,0};
    for (int i = 0; i < 32; ++i) {
        ssize_t bytes = read(fd, data, sizeof(data));
        if (bytes < 0) {
            perror("read mice");
            break;
        }

        if (!(fix_mouse[0] && fix_mouse[1] && fix_mouse[2])) {
            if (data[0] & 1) { fix_mouse[0] = true; }
            if (data[0] & 2) { fix_mouse[1] = true; }
            if (data[0] & 4) { fix_mouse[2] = true; }

            continue;
        }

        int8_t dx = data[1];
        int8_t dy = data[2];

        if (dx != 0) {
            if
        }

        printf("dx=%d ; dy=%d\n", dx, dy);
    }

    atomic_store(&exiting, true);

    close(fd);
end:
    return NULL;
}
*/

/*************************************************************************************************/

unsigned char *load_ppm(const char *filepath, uint32_t *width, uint32_t *height) {
    unsigned char *pixels = NULL;

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
    if (0 != strcmp(version, "P6")) {
        printf("bad PPM version\n");
        goto close;
    }

    // parse dimensions ///////////////////////////////////////////////////////////////////////////

    if (2 != fscanf(file, "%u %u\n", width, height)) {
        perror("fscanf width height");
        goto close;
    }
    if (0 == (*width) || 0 == (*height)) {
        printf("bad PPM dimensions\n");
        goto close;
    }
    uint32_t count = (*width) * (*height);

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
    unsigned long amount = (unsigned long) (end - start);
    unsigned long values = ((unsigned long) count) * 3;
    if (values != amount) {
        printf("bad PPM bytes (%lu!=%lu)\n", values, amount);
        goto close;
    }

    // alloc and read /////////////////////////////////////////////////////////////////////////////

    fseek(file, start, SEEK_SET);
    unsigned long bytes = amount * sizeof(char);
    pixels              = malloc(bytes);
    if (NULL == pixels) {
        perror("malloc");
        goto close;
    }
    if (0 == fread(pixels, sizeof(char), amount, file)) {
        perror("fread");
        goto free_pix;
    }

    // cleanup ////////////////////////////////////////////////////////////////////////////////////

    goto close;
free_pix:
    free(pixels);
close:
    fclose(file);
end:
    return pixels;
}

