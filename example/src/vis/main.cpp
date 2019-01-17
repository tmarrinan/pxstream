#include <iostream>
#include <chrono>
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <mpi.h>
#include "pxstream/client.h"
#include "jsobject.hpp"

// from glcorearb.h
#ifndef GL_EXT_texture_compression_s3tc
#define GL_EXT_texture_compression_s3tc 1
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT  0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3
#endif

//#define STREAM_RGBA

typedef struct Screen {
    int width;
    int height;
    int monitor;
    char title[96];
} Screen;
typedef struct GShaderProgram {
    GLuint program;
    GLint proj_uniform;
    GLint mv_uniform;
    GLint img_uniform;
} GShaderProgram;

void GetPixelLocations(int rank, jsvar& config, uint32_t global_width, uint32_t global_height, int32_t *local_px_size, int32_t *local_px_offset, int32_t *local_render_size, int32_t *local_render_offset);
void Init(int rank, GLFWwindow *window, Screen &screen, int32_t *local_px_size, int32_t *local_render_size, int32_t *local_render_offset, uint8_t *texture, GShaderProgram *shader, GLuint *vao, GLuint *tex_id);
void Render(GLFWwindow *window, GShaderProgram& shader, GLuint vao, GLuint tex_id);
GLuint CreateRectangleVao();
GShaderProgram CreateTextureShader();
GLint CompileShader(char *source, uint32_t length, GLint type);
void CreateShaderProgram(GLint vertex_shader, GLint fragment_shader, GLuint *program);
void LinkShaderProgram(GLuint program);
int32_t ReadFile(const char* filename, char** data_ptr);
uint64_t GetCurrentTime();

GLuint vertex_position_attrib = 0;
GLuint vertex_texcoord_attrib = 1;
glm::mat4 mat_projection;
glm::mat4 mat_modelview;

int main(int argc, char **argv)
{
    // initialize MPI
    int rc, rank, num_ranks;
    rc = MPI_Init(&argc, &argv);
    rc |= MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    rc |= MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);
    if (rc != 0)
    {
        fprintf(stderr, "Error initializing MPI and obtaining task ID information\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }


    // read config file
    jsvar config = jsobject::parseFromFile("example/resrc/config/laptop4-cfg.json");
    if (num_ranks != config["screen"]["displays"].length())
    {
        fprintf(stderr, "Error: app configured for %d ranks\n", config["screen"]["displays"].length());
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    jsvar display = config["screen"]["displays"][rank];
    if (display["location"].hasProperty("xdisplay"))
    {
        setenv("DISPLAY", ((std::string)(display["location"]["xdisplay"])).c_str(), true);
    }
    printf("[rank %d] start\n", rank);


    // PxStream clients
    if (argc < 3)
    {
        fprintf(stderr, "Error: no host and port provided for PxStream server (rank 0)\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    PxStream::Client stream(argv[1], atoi(argv[2]), MPI_COMM_WORLD);

    uint32_t global_width, global_height;
    stream.GetGlobalDimensions(&global_width, &global_height);

    int32_t local_px_size[2];
    int32_t local_px_offset[2];
    int32_t local_render_size[2];
    int32_t local_render_offset[2];
    GetPixelLocations(rank, config, global_width, global_height, local_px_size, local_px_offset, local_render_size, local_render_offset);
    DDR_DataDescriptor *selection = stream.CreateGlobalPixelSelection(local_px_size, local_px_offset);

    printf("[rank %d] %d %d, %dx%d\n", rank, local_px_offset[0], local_px_offset[1], local_px_size[0], local_px_size[1]);

#ifdef STREAM_RGBA
    uint32_t total_size = global_width * global_height * 4; // RGBA
    uint32_t img_size = local_px_size[0] * local_px_size[1] * 4; // RGBA
#else
    uint32_t total_size = global_width * global_height / 2; // DXT1
    uint32_t img_size = local_px_size[0] * local_px_size[1] / 2; // DXT1
#endif
    uint8_t *texture = new uint8_t[img_size];
    memset(texture, 0, img_size);


    // initialize GLFW
    if (!glfwInit())
    {
        exit(1);
    }

    // retrieve monitors
    int i, j, count, xpos1, xpos2, ypos1, ypos2;
    GLFWmonitor **monitors = glfwGetMonitors(&count);
    for (i = 0; i < count - 1; i++)
    {
        for (j = i + 1; j < count; j++)
        {
            glfwGetMonitorPos(monitors[j - 1], &xpos1, &ypos1);
            glfwGetMonitorPos(monitors[j], &xpos2, &ypos2);
            if (ypos2 < ypos1 || (ypos2 == ypos1 && xpos2 < xpos1))
            {
                GLFWmonitor *tmp_m = monitors[j];
                monitors[j] = monitors[j - 1];
                monitors[j - 1] = tmp_m;
            }
        }
    }
    
    bool fullscreen = false;
    if (display["location"].hasProperty("fullscreen"))
    {
        if (display["location"]["fullscreen"])
        {
            fullscreen = true;
        }
    }

    // define screen properties
    Screen screen;
    screen.width = display["width"];
    screen.height = display["height"];
    screen.monitor = display["location"]["monitor"];
    if (screen.monitor < 0 || screen.monitor >= count)
    {
        fprintf(stderr, "[rank %d] Error: cannot use monitor %d. Valid monitors are 0-%d. Using default monitor 0 instead.\n", rank, screen.monitor, count - 1);
        screen.monitor = 0;
    }
    strcpy(screen.title, (std::string("PxStream Vis: ") + std::to_string(rank)).c_str());

    // create a window and its OpenGL context
    GLFWwindow *window;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (fullscreen)
    {
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        window = glfwCreateWindow(screen.width, screen.height, screen.title, monitors[screen.monitor], NULL); 
    }
    else
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        window = glfwCreateWindow(screen.width-1, screen.height-1, screen.title, NULL, NULL);
        glfwPollEvents();
        glfwGetMonitorPos(monitors[screen.monitor], &xpos1, &ypos1);
        glfwSetWindowSize(window, screen.width, screen.height);
        glfwSetWindowPos(window, xpos1 + (int)display["location"]["x"], ypos1 + (int)display["location"]["y"]);
        glfwShowWindow(window);
    }

    // make window's context current
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // initialize Glad OpenGL extension handling
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        exit(1);
    }


    // initialize app
    GShaderProgram shader;
    GLuint vao;
    GLuint tex_id;
    Init(rank, window, screen, local_px_size, local_render_size, local_render_offset, texture, &shader, &vao, &tex_id);
    glfwPollEvents();

    MPI_Barrier(MPI_COMM_WORLD);
    
    if (display["location"].hasProperty("maindisplay"))
    {
        if (display["location"]["maindisplay"])
	{
	    glfwFocusWindow(window);
	}
    }

    
    // main event loop
    bool first_frame = true;
    bool stream_done = false;
    int num_frames = 0;
    double start = MPI_Wtime();
    double fps_start = start;
    double redist_time = 0.0;
    double read_time = 0.0;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (!stream_done)
        {
            double read_start = MPI_Wtime();
            stream.Read();
            double read_elapsed = MPI_Wtime() - read_start;
            double read_max_time;
            MPI_Reduce(&read_elapsed, &read_max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
            if (rank == 0)
            {
                if (!first_frame) read_time += read_max_time;
            }

            MPI_Barrier(MPI_COMM_WORLD);
            double redist_start = MPI_Wtime();
            stream.FillSelection(selection, texture);
            double redist_elapsed = MPI_Wtime() - redist_start;
            double redist_max_time;
            MPI_Reduce(&redist_elapsed, &redist_max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
            if (rank == 0)
            {
                redist_time += redist_max_time;
            }

            glBindTexture(GL_TEXTURE_2D, tex_id);
#ifdef STREAM_RGBA
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, local_px_size[0], local_px_size[1], GL_RGBA, GL_UNSIGNED_BYTE, texture); // RGBA
#else
            glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, local_px_size[0], local_px_size[1], GL_COMPRESSED_RGB_S3TC_DXT1_EXT, local_px_size[0] * local_px_size[1] / 2, texture); // DXT1
#endif
            glBindTexture(GL_TEXTURE_2D, 0);
            Render(window, shader, vao, tex_id);

            if (num_frames % 12 == 11)
            {
                double fps_end = MPI_Wtime();
                double elapsed_time[2] = {fps_end - fps_start, fps_end - start};
                double max_elapsed[2];
                MPI_Reduce(elapsed_time, max_elapsed, 2, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
                if (rank == 0)
                {
                    double recent_fps = 12.0 / max_elapsed[0];
                    double recent_mbps = ((double)((uint64_t)total_size * 8ULL * 12ULL) / (1024.0 * 1024.0)) / max_elapsed[0];
                    double overall_fps = (double)(num_frames+1) / max_elapsed[1];
                    double overall_mbps = ((double)((uint64_t)total_size * 8ULL * (uint64_t)(num_frames+1)) / (1024.0 * 1024.0)) / max_elapsed[1];
                    printf("[PxVis] last 12 frames: %.3lf fps / %.3lf mbps, overall (all %d frames): %.3lf fps / %.3lf mbps\n", recent_fps, recent_mbps, num_frames + 1, overall_fps, overall_mbps);
                }
                fps_start = MPI_Wtime();
            }

            //usleep(200000);

            if (stream.ServerFinished())
            {
                stream_done = true;
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            num_frames++;
        }
        first_frame = false;
    }

    double elapsed = MPI_Wtime() - start;
    double max_time;
    MPI_Reduce(&elapsed, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    if (rank == 0)
    {
        uint64_t overall_data = (uint64_t)total_size * 8LL * (uint64_t)num_frames;//4LL * 8LL * 26LL;
        double speed = (double)overall_data / max_time;
        printf("[PxVis] finished - received %d frames in %.3lf secs (%.3lf Mbps)\n", num_frames, max_time, speed / (1024.0 * 1024.0));
        printf("[PxVis] redistribution time: %.3lf sec (%.3lf sec per frame)\n", redist_time, redist_time / (double)num_frames);
    }

    
    // finalize
    glfwDestroyWindow(window);
    glfwTerminate();
    MPI_Finalize();
    
    return 0;
}

void GetPixelLocations(int rank, jsvar& config, uint32_t global_width, uint32_t global_height, int32_t *local_px_size, int32_t *local_px_offset, int32_t *local_render_size, int32_t *local_render_offset)
{
    jsvar display = config["screen"]["displays"][rank];
    double px_aspect = (double)global_width / (double)global_height;
    double display_aspect = (double)config["screen"]["resolution"]["width"] / (double)config["screen"]["resolution"]["height"];
    if (rank == 0) printf("[PxVis] image aspect: %.3lf, display aspect: %.3lf\n", px_aspect, display_aspect);
    local_render_size[0] = display["width"];
    local_render_size[1] = display["height"];
    local_render_offset[0] = 0;
    local_render_offset[1] = 0;
    if (px_aspect < display_aspect)
    {
        double scale = (double)global_height / (double)config["screen"]["resolution"]["height"];
        int32_t d_offset_x = ((int32_t)((double)global_height * display_aspect) - global_width) / 2;
        int32_t d_width = global_width;
        local_px_size[0] = (int32_t)((double)display["width"] * scale);
        local_px_size[1] = (int32_t)((double)display["height"] * scale);
        local_px_offset[0] = (int32_t)((double)display["x"] * scale);
        local_px_offset[1] = (int32_t)((double)display["y"] * scale);
        if (d_offset_x > local_px_offset[0])
        {
            local_render_offset[0] = (int32_t)((double)(d_offset_x - local_px_offset[0]) / scale);
            local_render_size[0] -= local_render_offset[0];
            local_px_size[0] -= d_offset_x - local_px_offset[0];
            local_px_offset[0] = d_offset_x;
        }
        if (d_offset_x + d_width < local_px_offset[0] + local_px_size[0])
        {
            local_render_size[0] -= (int32_t)((double)((local_px_offset[0] + local_px_size[0]) - (d_offset_x + d_width)) / scale);
            local_px_size[0] = d_offset_x + d_width - local_px_offset[0];
        }
        local_px_offset[0] -= d_offset_x;
    }
    else
    {
        double scale = (double)global_width / (double)config["screen"]["resolution"]["width"];
        int32_t d_offset_y = ((int32_t)((double)global_width / display_aspect) - global_height) / 2;
        int32_t d_height = global_height;
        local_px_size[0] = (int32_t)((double)display["width"] * scale);
        local_px_size[1] = (int32_t)((double)display["height"] * scale);
        local_px_offset[0] = (int32_t)((double)display["x"] * scale);
        local_px_offset[1] = (int32_t)((double)display["y"] * scale);
        if (d_offset_y > local_px_offset[1])
        {
            local_render_offset[1] = (int32_t)((double)(d_offset_y - local_px_offset[1]) / scale);
            local_render_size[1] -= local_render_offset[1];
            local_px_size[1] -= d_offset_y - local_px_offset[1];
            local_px_offset[1] = d_offset_y;
        }
        if (d_offset_y + d_height < local_px_offset[1] + local_px_size[1])
        {
            local_render_size[1] -= (int32_t)((double)((local_px_offset[1] + local_px_size[1]) - (d_offset_y + d_height)) / scale);
            local_px_size[1] = d_offset_y + d_height - local_px_offset[1];
        }
        local_px_offset[1] -= d_offset_y;
    }

    if (local_render_size[0] < 0)
    {
        local_render_size[0] = 0;
        local_render_size[1] = 0;
    }
    if (local_render_size[1] < 0)
    {
        local_render_size[0] = 0;
        local_render_size[1] = 0;
    }
    if (local_render_offset[0] < 0)
    {
        local_render_offset[0] = 0;
    }
    if (local_render_offset[1] < 0)
    {
        local_render_offset[1] = 0;
    }
#ifndef STREAM_RGBA
    local_px_size[0] -= local_px_size[0] % 4;
    local_px_size[1] -= local_px_size[1] % 4;
    local_px_offset[0] -= local_px_offset[0] % 4;
    local_px_offset[1] -= local_px_offset[1] % 4;
#endif
}

void Init(int rank, GLFWwindow *window, Screen &screen, int32_t *local_px_size, int32_t *local_render_size, int32_t *local_render_offset, uint8_t *texture, GShaderProgram *shader, GLuint *vao, GLuint *tex_id)
{
    /*
    // print supported extensions
    GLint n = 0; 
    glGetIntegerv(GL_NUM_EXTENSIONS, &n); 
    for (GLint i = 0; i < n; i++) 
    { 
        const char* extension = (const char*)glGetStringi(GL_EXTENSIONS, i);
        printf("Ext %d: %s\n", i, extension);
    }
    */

    int w, h;
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glEnable(GL_DEPTH_TEST);
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    if (rank == 0) printf("[PxVis] OpenGL: %s\n", glGetString(GL_VERSION));

    *shader = CreateTextureShader();
    *vao = CreateRectangleVao();

    glGenTextures(1, tex_id);
    glBindTexture(GL_TEXTURE_2D, *tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#ifdef STREAM_RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, local_px_size[0], local_px_size[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, texture); // RGBA
#else
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, local_px_size[0], local_px_size[1], 0, local_px_size[0] * local_px_size[1] / 2, texture); //DXT1
#endif
    glBindTexture(GL_TEXTURE_2D, 0);


#ifdef STREAM_RGBA
    mat_projection = glm::ortho(0.0, (double)screen.width, (double)screen.height, 0.0, 1.0, -1.0); // RGBA
    mat_modelview = glm::translate(glm::mat4(1.0), glm::vec3(local_render_offset[0], local_render_offset[1], 0.0));
    printf("[rank %d] render: %d %d, %dx%d\n", rank, local_render_offset[0], local_render_offset[1], local_render_size[0], local_render_size[1]);
#else
    mat_projection = glm::ortho(0.0, (double)screen.width, 0.0, (double)screen.height, 1.0, -1.0); // DXT1
    mat_modelview = glm::translate(glm::mat4(1.0), glm::vec3(local_render_offset[0], screen.height - local_render_size[1] - local_render_offset[1], 0.0));
    printf("[rank %d] render: %d %d, %dx%d\n", rank, local_render_offset[0], screen.height - local_render_size[1] - local_render_offset[1], local_render_size[0], local_render_size[1]);
#endif
    mat_modelview = glm::scale(mat_modelview, glm::vec3(local_render_size[0], local_render_size[1], 1.0));

    Render(window, *shader, *vao, *tex_id);
}

void Render(GLFWwindow *window, GShaderProgram& shader, GLuint vao, GLuint tex_id)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader.program);
    glUniformMatrix4fv(shader.proj_uniform, 1, GL_FALSE, glm::value_ptr(mat_projection));
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
    glUniformMatrix4fv(shader.mv_uniform, 1, GL_FALSE, glm::value_ptr(mat_modelview));
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glUniform1i(shader.img_uniform, 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    MPI_Barrier(MPI_COMM_WORLD);

    glfwSwapBuffers(window);
}

GLuint CreateRectangleVao()
{
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // vertices
    GLuint vertex_position_buffer;
    glGenBuffers(1, &vertex_position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_position_buffer);
    GLfloat vertices[12] = {
        0.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        1.0, 0.0, 0.0,
        1.0, 1.0, 0.0
    };
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(vertex_position_attrib);
    glVertexAttribPointer(vertex_position_attrib, 3, GL_FLOAT, false, 0, 0);

    // textures
    GLuint vertex_texcoord_buffer;
    glGenBuffers(1, &vertex_texcoord_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_texcoord_buffer);
    GLfloat texcoords[8] = {
        0.0, 0.0,
        0.0, 1.0,
        1.0, 0.0,
        1.0, 1.0
    };
    glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(GLfloat), texcoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(vertex_texcoord_attrib);
    glVertexAttribPointer(vertex_texcoord_attrib, 2, GL_FLOAT, false, 0, 0);

    // faces of the triangles
    GLuint vertex_index_buffer;
    glGenBuffers(1, &vertex_index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertex_index_buffer);
    GLushort indices[6] = {
        0, 3, 1,
        3, 0, 2
    };
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6 * sizeof(GLushort), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    return vao;
}

GShaderProgram CreateTextureShader()
{
    GShaderProgram shader;

    const char *vertex_file = "example/resrc/shaders/texture.vert";
    char *vertex_src;
    int32_t vertex_src_length = ReadFile(vertex_file, &vertex_src);
    GLint vertex_shader = CompileShader(vertex_src, vertex_src_length, GL_VERTEX_SHADER);
    free(vertex_src);

    const char *fragment_file = "example/resrc/shaders/texture.frag";
    char *fragment_src;
    int32_t fragment_src_length = ReadFile(fragment_file, &fragment_src);
    GLint fragment_shader = CompileShader(fragment_src, fragment_src_length, GL_FRAGMENT_SHADER);
    free(fragment_src);

    CreateShaderProgram(vertex_shader, fragment_shader, &shader.program);

    glBindAttribLocation(shader.program, vertex_position_attrib, "aVertexPosition");
    glBindAttribLocation(shader.program, vertex_texcoord_attrib, "aVertexTexCoord");
    glBindAttribLocation(shader.program, 0, "FragColor");

    LinkShaderProgram(shader.program);

    shader.mv_uniform = glGetUniformLocation(shader.program, "uModelViewMatrix");
    shader.proj_uniform = glGetUniformLocation(shader.program, "uProjectionMatrix");
    shader.img_uniform = glGetUniformLocation(shader.program, "uImage");

    return shader;
}

GLint CompileShader(char *source, uint32_t length, GLint type)
{
    GLint status;
    GLint shader = glCreateShader(type);

    const char *src_bytes = source;
    const GLint len = length;
    glShaderSource(shader, 1, &src_bytes, &len);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == 0)
    {
        GLint log_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        char *info = (char*)malloc(log_length + 1);
        glGetShaderInfoLog(shader, log_length, NULL, info);
        fprintf(stderr, "Error: failed to compile shader:\n%s\n", info);
        free(info);

        return -1;
    }

    return shader;
}

void CreateShaderProgram(GLint vertex_shader, GLint fragment_shader, GLuint *program)
{
    *program = glCreateProgram();
    glAttachShader(*program, vertex_shader);
    glAttachShader(*program, fragment_shader);
}

void LinkShaderProgram(GLuint program)
{
    GLint status;
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == 0)
    {
        fprintf(stderr, "Error: unable to initialize shader program\n");
    }
}

int32_t ReadFile(const char* filename, char** data_ptr)
{
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "Error: cannot open %s\n", filename);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    int32_t fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    *data_ptr = (char*)malloc(fsize);
    size_t read = fread(*data_ptr, fsize, 1, fp);
    if (read != 1)
    {
        fprintf(stderr, "Error cannot read %s\n", filename);
        return -1;
    }

    fclose(fp);

    return fsize;
}

uint64_t GetCurrentTime()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
