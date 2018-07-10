#include <iostream>
#include <netsocket/client.h>
#include <mpi.h>
#include <ddr.h>
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

typedef struct Screen {
    int width;
    int height;
    char title[96];
} Screen;

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

    // initialize GLFW
    if (!glfwInit())
    {
        exit(1);
    }

    // define screen properties
    Screen screen;
    screen.width = 1280;
    screen.height = 720;
    strcpy(screen.title, "PxStream Client");

    // create a window and its OpenGL context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(screen.width, screen.height, screen.title, NULL, NULL);

    // make window's context current
    glfwMakeContextCurrent(window);

    // initialize Glad OpenGL extension handling
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        exit(1);
    }

    // main event loop
    while (!glfwWindowShouldClose(window))
    {
        glfwWaitEvents();
    }

    // finalize
    glfwDestroyWindow(window);
    glfwTerminate();
    MPI_Finalize();

    return 0;
}