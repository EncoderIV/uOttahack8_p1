/*
 * Copyright (c) 2024, BlackBerry Limited. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jpeglib.h>

int compress_to_jpeg(uint8_t* rgb_data, int width, int height, uint8_t** jpeg_data, unsigned long* jpeg_size) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, jpeg_data, jpeg_size);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 75, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &rgb_data[cinfo.next_scanline * width * 3];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return 1;
}

#include <camera/camera_api.h>

/**
 * @brief Number of channels for supported frametypes
 */
#define NUM_CHANNELS (3)

/**
 * @brief List of frametypes that @c processCameraData can operate on
 */
const camera_frametype_t cSupportedFrametypes[] = {
    CAMERA_FRAMETYPE_YCBYCR,
    CAMERA_FRAMETYPE_CBYCRY,
    CAMERA_FRAMETYPE_RGB8888,
    CAMERA_FRAMETYPE_BGR8888,
};
#define NUM_SUPPORTED_FRAMETYPES (sizeof(cSupportedFrametypes) / sizeof(cSupportedFrametypes[0]))

typedef struct {
    camera_frametype_t frametype;
    camera_framedesc_t framedesc;
    char* shm_name;
    uint8_t* mapped_data;
    size_t data_size;
} Frame;

typedef struct {
    camera_frametype_t frametype;
    uint32_t width;
    uint32_t height;
    size_t size;
} Metadata;

#define MAX_FRAMES 5
static Frame frame_buffer[MAX_FRAMES];
static int buffer_head = 0;
static int buffer_count = 0;
static pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;

static Metadata* metadata_mapped = NULL;
static int metadata_fd = -1;
static char* latest_shm_name = NULL;
static uint8_t* latest_mapped = NULL;
static size_t latest_size = 0;
static int sock = -1;

/**
 * @brief Prints a list of available cameras
 */
static void listAvailableCameras(void);

/**
 * @brief Callback function to be called when camera data is available
 *
 * @param handle Handle to the camera providing the data
 * @param buffer Buffer of camera data
 * @param arg Argument provided when starting streaming
 */
static void processCameraData(camera_handle_t handle, camera_buffer_t* buffer, void* arg);

/**
 * @brief Blocks until the user presses any key
 */
static void blockOnKeyPress(void);

/**
 * @brief Calculates the size of the frame data in bytes
 */
static size_t get_frame_size(camera_buffer_t* buffer);

/**
 * @brief Returns the most recent frame from the buffer
 */
static Frame* get_latest_frame(void);

static size_t get_frame_size(camera_buffer_t* buffer) {
    switch (buffer->frametype) {
    case CAMERA_FRAMETYPE_RGB8888:
        return (size_t)buffer->framedesc.rgb8888.width * buffer->framedesc.rgb8888.height * 4;
    case CAMERA_FRAMETYPE_BGR8888:
        return (size_t)buffer->framedesc.bgr8888.width * buffer->framedesc.bgr8888.height * 4;
    case CAMERA_FRAMETYPE_YCBYCR:
        return (size_t)buffer->framedesc.ycbycr.width * buffer->framedesc.ycbycr.height * 2;
    case CAMERA_FRAMETYPE_CBYCRY:
        return (size_t)buffer->framedesc.cbycry.width * buffer->framedesc.cbycry.height * 2;
    default:
        return 0;
    }
}

static Frame* get_latest_frame(void) {
    pthread_mutex_lock(&frame_mutex);
    if (buffer_count == 0) {
        pthread_mutex_unlock(&frame_mutex);
        return NULL;
    }
    int latest_index = (buffer_head + buffer_count - 1) % MAX_FRAMES;
    pthread_mutex_unlock(&frame_mutex);
    return &frame_buffer[latest_index];
}

int main(int argc, char* argv[])
{
    int err;
    int opt;
    camera_unit_t unit = CAMERA_UNIT_NONE;
    camera_handle_t handle = CAMERA_HANDLE_INVALID;
    camera_frametype_t frametype = CAMERA_FRAMETYPE_UNSPECIFIED;

    // Read command line options
    while ((opt = getopt(argc, argv, "u:")) != -1 || (optind < argc)) {
        switch (opt) {
        case 'u':
            unit = (camera_unit_t)strtol(optarg, NULL, 10);
            break;
        default:
            printf("Ignoring unrecognized option: %s\n", optarg);
            break;
        }
    }

    // If no camera unit has been specified, list the options and exit
    if ((unit == CAMERA_UNIT_NONE) || (unit >= CAMERA_UNIT_NUM_UNITS)) {
        listAvailableCameras();
        printf("Please provide camera unit with -u option\n");
        exit(EXIT_SUCCESS);
    }

    // Open a read-only handle for the specified camera unit.
    // CAMERA_MODE_RO doesn't give us access to change camera configuration
    // and we can't modify the memory in a provided buffer.
    err = camera_open(unit, CAMERA_MODE_RO, &handle);
    if ((err != CAMERA_EOK) || (handle == CAMERA_HANDLE_INVALID)) {
        printf("Failed to open CAMERA_UNIT_%d: err = %d\n", (int)unit, err);
        exit(EXIT_FAILURE);
    }

    // Create shared memory for metadata
    metadata_fd = shm_open("/camera_metadata", O_CREAT | O_RDWR, 0666);
    if (metadata_fd == -1) {
        printf("Failed to create metadata shm\n");
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }
    if (ftruncate(metadata_fd, sizeof(Metadata)) == -1) {
        printf("Failed to truncate metadata\n");
        close(metadata_fd);
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }
    metadata_mapped = mmap(NULL, sizeof(Metadata), PROT_READ | PROT_WRITE, MAP_SHARED, metadata_fd, 0);
    if (metadata_mapped == MAP_FAILED) {
        printf("Failed to mmap metadata\n");
        close(metadata_fd);
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }
    close(metadata_fd);
    metadata_fd = -1;

    // Connect to host for JPEG streaming
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Failed to create socket\n");
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("192.168.1.100"); // Change to host IP
    server.sin_port = htons(5001);
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Failed to connect to host\n");
        close(sock);
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }

    // Make sure that this camera defaults to a supported frametype
    err = camera_get_vf_property(handle, CAMERA_IMGPROP_FORMAT, &frametype);
    if (err != CAMERA_EOK) {
        printf("Failed to get frametype for CAMERA_UNIT_%d: err = %d\n", (int)unit, err);
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }
    bool unsupportedFrametype = true;
    for (uint i = 0; i < NUM_SUPPORTED_FRAMETYPES; i++) {
        if (frametype == cSupportedFrametypes[i]) {
            unsupportedFrametype = false;
            break;
        }
    }
    if (unsupportedFrametype) {
        printf("Camera frametype %d is not supported\n", (int)frametype);
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }
    printf("\n");

    // Start the camera streaming: callbacks will start being received
    err = camera_start_viewfinder(handle, processCameraData, NULL, NULL);
    if (err != CAMERA_EOK) {
        printf("Failed to start CAMERA_UNIT_%d: err = %d\n", (int)unit, err);
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }

    blockOnKeyPress();

    // Stop the camera streaming: no more callbacks will be received
    err = camera_stop_viewfinder(handle);
    printf("\r\n");
    if (err != CAMERA_EOK) {
        printf("Failed to stop CAMERA_UNIT_%d: err = %d\n", (int)unit, err);
        (void)camera_close(handle);
        exit(EXIT_FAILURE);
    }

    // Close the camera handle
    err = camera_close(handle);
    if (err != CAMERA_EOK) {
        printf("Failed to close CAMERA_UNIT_%d: err = %d\n", (int)unit, err);
        exit(EXIT_FAILURE);
    }

    // Free frame buffer
    for (int i = 0; i < buffer_count; i++) {
        int idx = (buffer_head + i) % MAX_FRAMES;
        munmap(frame_buffer[idx].mapped_data, frame_buffer[idx].data_size);
        shm_unlink(frame_buffer[idx].shm_name);
        free(frame_buffer[idx].shm_name);
    }

    // Free shared memory
    if (metadata_mapped) {
        munmap(metadata_mapped, sizeof(Metadata));
        shm_unlink("/camera_metadata");
    }
    if (latest_mapped) {
        munmap(latest_mapped, latest_size);
        shm_unlink(latest_shm_name);
        free(latest_shm_name);
    }
    shm_unlink("/camera_latest_name");
    if (sock != -1) {
        close(sock);
    }

    exit(EXIT_SUCCESS);
}

static void listAvailableCameras(void)
{
    int err;
    uint numSupported;
    camera_unit_t* supportedCameras;

    // Determine how many cameras are supported
    err = camera_get_supported_cameras(0, &numSupported, NULL);
    if (err != CAMERA_EOK) {
        printf("Failed to get number of supported cameras: err = %d\n", err);
        return;
    }

    if (numSupported == 0) {
        printf("No supported cameras detected!\n");
        return;
    }

    // Allocate an array big enough to hold all camera units
    supportedCameras = (camera_unit_t*)calloc(numSupported, sizeof(camera_unit_t));
    if (supportedCameras == NULL) {
        printf("Failed to allocate memory for supported cameras\n");
        return;
    }

    // Get the list of supported cameras
    err = camera_get_supported_cameras(numSupported, &numSupported, supportedCameras);
    if (err != CAMERA_EOK) {
        printf("Failed to get list of supported cameras: err = %d\n", err);
    } else {
        printf("Available camera units:\n");
        for (uint i = 0; i < numSupported; i++) {
            printf("\tCAMERA_UNIT_%d", supportedCameras[i]);
            printf(" (specify -u %d)\n", supportedCameras[i]);
        }
    }

    free(supportedCameras);
    return;
}

static void processCameraData(camera_handle_t handle, camera_buffer_t* buffer, void* arg)
{
    clock_t begin;
    clock_t end;
    double channelAverage[NUM_CHANNELS];

    // No need for handle or argument data
    (void)handle;
    (void)arg;

    // Store frame in buffer
    pthread_mutex_lock(&frame_mutex);
    size_t size = get_frame_size(buffer);
    if (buffer_count == MAX_FRAMES) {
        // Free oldest frame
        munmap(frame_buffer[buffer_head].mapped_data, frame_buffer[buffer_head].data_size);
        shm_unlink(frame_buffer[buffer_head].shm_name);
        free(frame_buffer[buffer_head].shm_name);
        buffer_head = (buffer_head + 1) % MAX_FRAMES;
        buffer_count--;
    }
    int index = (buffer_head + buffer_count) % MAX_FRAMES;
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/camera_frame_%d", index);
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd != -1) {
        if (ftruncate(fd, size) != -1) {
            uint8_t* mapped_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (mapped_data != MAP_FAILED) {
                memcpy(mapped_data, buffer->framebuf, size);
                frame_buffer[index].frametype = buffer->frametype;
                frame_buffer[index].framedesc = buffer->framedesc;
                frame_buffer[index].shm_name = strdup(shm_name);
                frame_buffer[index].mapped_data = mapped_data;
                frame_buffer[index].data_size = size;
                buffer_count++;
            }
            close(fd);
        } else {
            close(fd);
        }
    }
    pthread_mutex_unlock(&frame_mutex);

    // Update latest shared memory
    if (latest_mapped) {
        munmap(latest_mapped, latest_size);
        shm_unlink(latest_shm_name);
        free(latest_shm_name);
    }
    latest_shm_name = strdup("/camera_latest");
    int latest_fd = shm_open(latest_shm_name, O_CREAT | O_RDWR, 0666);
    if (latest_fd != -1) {
        size_t size = get_frame_size(buffer);
        if (ftruncate(latest_fd, size) != -1) {
            latest_mapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, latest_fd, 0);
            if (latest_mapped != MAP_FAILED) {
                memcpy(latest_mapped, buffer->framebuf, size);
                latest_size = size;
            }
            close(latest_fd);
        } else {
            close(latest_fd);
        }
    }

    // Update metadata
    uint32_t width = 0, height = 0;
    switch (buffer->frametype) {
    case CAMERA_FRAMETYPE_RGB8888:
        width = buffer->framedesc.rgb8888.width;
        height = buffer->framedesc.rgb8888.height;
        break;
    case CAMERA_FRAMETYPE_BGR8888:
        width = buffer->framedesc.bgr8888.width;
        height = buffer->framedesc.bgr8888.height;
        break;
    case CAMERA_FRAMETYPE_YCBYCR:
        width = buffer->framedesc.ycbycr.width;
        height = buffer->framedesc.ycbycr.height;
        break;
    case CAMERA_FRAMETYPE_CBYCRY:
        width = buffer->framedesc.cbycry.width;
        height = buffer->framedesc.cbycry.height;
        break;
    default:
        width = 0;
        height = 0;
        break;
    }
    metadata_mapped->frametype = buffer->frametype;
    metadata_mapped->width = width;
    metadata_mapped->height = height;
    metadata_mapped->size = size;

    // Update latest name
    int name_fd = shm_open("/camera_latest_name", O_CREAT | O_RDWR, 0666);
    if (name_fd != -1) {
        if (ftruncate(name_fd, 256) != -1) {
            char* name_mapped = mmap(NULL, 256, PROT_READ | PROT_WRITE, MAP_SHARED, name_fd, 0);
            if (name_mapped != MAP_FAILED) {
                strncpy(name_mapped, "/camera_latest", 256);
                munmap(name_mapped, 256);
            }
        }
        close(name_fd);
    }

    // Camera data is buffer->framebuf and described by buffer->framedesc.
    // As an example, let's compute channel averages by iterating over the
    // bytes in each line and determining which channel the byte belongs to.

    // Get dimensions
    uint32_t width = 0, height = 0, stride = 0;
    switch (buffer->frametype) {
    case CAMERA_FRAMETYPE_RGB8888:
        width = buffer->framedesc.rgb8888.width;
        height = buffer->framedesc.rgb8888.height;
        stride = buffer->framedesc.rgb8888.stride;
        break;
    case CAMERA_FRAMETYPE_BGR8888:
        width = buffer->framedesc.bgr8888.width;
        height = buffer->framedesc.bgr8888.height;
        stride = buffer->framedesc.bgr8888.stride;
        break;
    case CAMERA_FRAMETYPE_YCBYCR:
        width = buffer->framedesc.ycbycr.width;
        height = buffer->framedesc.ycbycr.height;
        stride = buffer->framedesc.ycbycr.stride;
        break;
    case CAMERA_FRAMETYPE_CBYCRY:
        width = buffer->framedesc.cbycry.width;
        height = buffer->framedesc.cbycry.height;
        stride = buffer->framedesc.cbycry.stride;
        break;
    default:
        width = 0;
        height = 0;
        stride = 0;
        break;
    }

    // Compress to JPEG and send if RGB8888
    if (buffer->frametype == CAMERA_FRAMETYPE_RGB8888 && width > 0 && height > 0) {
        uint8_t* rgb_data = malloc(width * height * 3);
        if (rgb_data) {
            for (uint32_t y = 0; y < height; y++) {
                for (uint32_t x = 0; x < width; x++) {
                    rgb_data[(y * width + x) * 3] = buffer->framebuf[y * stride + x * 4];
                    rgb_data[(y * width + x) * 3 + 1] = buffer->framebuf[y * stride + x * 4 + 1];
                    rgb_data[(y * width + x) * 3 + 2] = buffer->framebuf[y * stride + x * 4 + 2];
                }
            }
            uint8_t* jpeg_data = NULL;
            unsigned long jpeg_size = 0;
            compress_to_jpeg(rgb_data, width, height, &jpeg_data, &jpeg_size);
            free(rgb_data);
            if (jpeg_data) {
                send(sock, &jpeg_size, sizeof(unsigned long), 0);
                send(sock, jpeg_data, jpeg_size, 0);
                free(jpeg_data);
            }
        }
    }

    begin = clock();
    memset(channelAverage, 0x0, sizeof(channelAverage));
    switch (buffer->frametype) {
    case CAMERA_FRAMETYPE_RGB8888:
    {
        // Channel averages ordering: R, G, B
        for (uint y = 0; y < height; y++) {
            uint8_t* linePointer = buffer->framebuf + y * stride;
            for (uint i = 0; i < 4 * width; i++) {
                uint chan = i % 4;
                if (chan == 3) {
                    continue;
                }
                channelAverage[chan] += (double)*(linePointer + i);
            }
        }
        for (uint chan = 0; chan < NUM_CHANNELS; chan++) {
            channelAverage[chan] /= (double)(width * height);
        }
        break;
    }
    case CAMERA_FRAMETYPE_BGR8888:
    {
        // Channel averages ordering: R, G, B
        for (uint y = 0; y < height; y++) {
            uint8_t* linePointer = buffer->framebuf + y * stride;
            for (uint i = 0; i < 4 * width; i++) {
                if (i % 4 == 3) {
                    continue;
                }
                uint chan = (4 * width - i + 3) % 4 - 1;
                channelAverage[chan] += (double)*(linePointer + i);
            }
        }
        for (uint chan = 0; chan < NUM_CHANNELS; chan++) {
            channelAverage[chan] /= (double)(width * height);
        }
        break;
    }
    case CAMERA_FRAMETYPE_YCBYCR:
    {
        // Channel averages ordering: Y, Cb, Cr
        for (uint y = 0; y < height; y++) {
            uint8_t* linePointer = buffer->framebuf + y * stride;
            for (uint i = 0; i < 2 * width; i++) {
                uint chan = i % 2;
                if (chan == 1) {
                    chan = (i - 1) % 4 / 2 + 1;
                }
                channelAverage[chan] += (double)*(linePointer + i);
            }
        }
        channelAverage[0] /= (double)(width * height);
        channelAverage[1] /= (double)(width / 2 * height);
        channelAverage[2] /= (double)(width / 2 * height);
        break;
    }
    case CAMERA_FRAMETYPE_CBYCRY:
    {
        // Channel averages ordering: Y, Cb, Cr
        for (uint y = 0; y < height; y++) {
            uint8_t* linePointer = buffer->framebuf + y * stride;
            for (uint i = 0; i < 2 * width; i++) {
                uint chan = (i + 1) % 2;
                if (chan == 1) {
                    chan = ((i + 1) % 4 + 1) / 2;
                }
                channelAverage[chan] += (double)*(linePointer + i);
            }
        }
        channelAverage[0] /= (double)(width * height);
        channelAverage[1] /= (double)(width / 2 * height);
        channelAverage[2] /= (double)(width / 2 * height);
        break;
    }
    default:
        printf("\r");
        printf("Frametype %d is not suppported!", (int)buffer->frametype);
        printf(" (press any key to stop example)");
        fflush(stdout);
        return;
    }
    end = clock();

    printf("\r");
    printf("Channel averages: ");
    printf("%.3f, %.3f, %.3f", channelAverage[0], channelAverage[1], channelAverage[2]);
    printf(" took %.3f ms", (double)(end - begin) / CLOCKS_PER_SEC * 1000);
    printf(" (press any key to stop example)     ");
    fflush(stdout);

    return;
}

static void blockOnKeyPress(void)
{
    struct termios oldterm;
    struct termios newterm;
    char key;

    (void)tcgetattr(STDIN_FILENO, &oldterm);
    newterm = oldterm;
    newterm.c_lflag &= ~(ECHO | ICANON);
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &newterm);
    // Blocking call: wait for 1 byte of data to become available
    (void)read(STDIN_FILENO, &key, 1);
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &oldterm);

    return;
}
