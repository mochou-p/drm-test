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

struct mouse_arg {
    atomic_bool  *running;
    atomic_uint  *mouse_x;
    atomic_uint  *mouse_y;
    int          width;
    int          height;
};

/*************************************************************************************************/

unsigned char *load_ppm(const char *, uint32_t *, uint32_t *);

void *mouse_handler(void *);

/*************************************************************************************************/

int main(int argc, char **argv) {
    int exitcode = EXIT_FAILURE;

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

    // open cardX /////////////////////////////////////////////////////////////////////////////////

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

    // mouse handling /////////////////////////////////////////////////////////////////////////////

    atomic_bool running = true;
    atomic_uint mouse_x = width  / 2;
    atomic_uint mouse_y = height / 2;

    struct mouse_arg m_arg = { &running, &mouse_x, &mouse_y, (int) width, (int) height };

    pthread_t mouse_thread;
    pthread_create(&mouse_thread, NULL, mouse_handler, (void *) &m_arg);

    // main loop //////////////////////////////////////////////////////////////////////////////////

    uint32_t i, x, y;
    uint32_t *framebuffer = (uint32_t *) buffer;

    while (atomic_load(&running)) {
        // draw -----------------------------------------------------------------------------------

        x = atomic_load(&mouse_x);
        y = atomic_load(&mouse_y);
        i = y * width + x;

        framebuffer[i] = 0xFFFFFFFF;

        // present --------------------------------------------------------------------------------

        if (0 != drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &connector->connector_id, 1, &mode)) {
            perror("drmModeSetCrtc");
            goto join_mouse;
        }
    }

    // cleanup ////////////////////////////////////////////////////////////////////////////////////

    exitcode = EXIT_SUCCESS;
join_mouse:
    pthread_join(mouse_thread, NULL);
free_wall:
    free(wallpaper);
unmap_buf:
    munmap(buffer, c_dumb.size);
free_con:
    // FIXME: refactor to avoid `if`
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

void *mouse_handler(void *m_arg) {
    struct mouse_arg *arg = (struct mouse_arg *) m_arg;

    // open mice //////////////////////////////////////////////////////////////////////////////////

    int fd = open("/dev/input/mice", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/input/mice");
        goto end;
    }

    unsigned char data[3];
    // cppcheck-suppress variableScope
    ssize_t       bytes;
    // cppcheck-suppress variableScope
    int           dx, dy, new_x, new_y;
    int           x = arg->width  / 2;
    int           y = arg->height / 2;
    for (int i = 0; i < 500; ++i) {
        bytes = read(fd, data, sizeof(data));
        if (bytes < 0) {
            perror("read mice");
            break;
        }

        dx    = (int) (char) data[1];
        new_x = x + dx;
        if (new_x >= 0 && new_x < 1920) {
            x = new_x;
            atomic_store(arg->mouse_x, (unsigned int) x);
        }

        dy    = (int) (char) data[2];
        new_y = y - dy;
        if (new_y >= 0 && new_y < 1080) {
            y = new_y;
            atomic_store(arg->mouse_y, (unsigned int) y);
        }
    }

    close(fd);
end:
    atomic_store(arg->running, false);
    return NULL;
}

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

