#include "camera.hpp"
#include "constants.hpp"
#include "coordinates.hpp"
#include "gameData.hpp"
#include "logger.hpp"
#include "screens.hpp"
#include "systems.hpp"
#include "worldFactory.hpp"

#include "graphics/line.hpp"
#include "graphics/meshObject.hpp"
#include "graphics/renderer.hpp"
#include "graphics/shaderProgram.hpp"

#include "imgui/imguiWrapper.hpp"

#include "xbak/FileManager.h"
#include "xbak/FileBuffer.h"
#include "xbak/PaletteResource.h"

#include <GL/glew.h>

#include <GLFW/glfw3.h>

#include "glm.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <boost/range/adaptor/indexed.hpp>

#include <functional>
#include <memory>

int main(int argc, char** argv)
{
    const auto& logger = Logging::LogState::GetLogger("main");
    Logging::LogState::SetLevel(Logging::LogLevel::Debug);
    //Logging::LogState::Disable("MeshObjectStore");
    //Logging::LogState::Disable("WorldTileStore");
    
    std::string saveFile{argv[1]};

    logger.Info() << "Loading save:" << saveFile << std::endl;

    /*auto fb = FileBufferFactory::CreateFileBuffer(saveFile);
    BAK::GameData gameData(fb);

    std::stringstream zoneSS{""};
    zoneSS << "Z" << std::setw(2) << std::setfill('0')<< gameData.mZone;
    std::string zone = zoneSS.str();*/

    BAK::ZoneLabel zoneLabel{argv[1]};

    BAK::DialogStore dialogStore{};
    dialogStore.Load();
    BAK::DialogIndex dialogIndex{zoneLabel};
    dialogIndex.Load();

    auto palz = std::make_unique<PaletteResource>();
    FileManager::GetInstance()->Load(palz.get(), zoneLabel.GetPalette());
    auto& pal = *palz->GetPalette();

    auto textureStore = BAK::TextureStore{zoneLabel, pal};
    auto zoneItems   = BAK::ZoneItemStore{zoneLabel, textureStore};
    auto worlds      = BAK::WorldTileStore{zoneItems};
    auto worldCenter = worlds.GetTiles().front().GetCenter();
    //auto loc = gameData.mLocus.mPosition;
    //auto worldCenter = glm::vec3{loc.x, 1.6, loc.y};

    auto objStore = Graphics::MeshObjectStorage{};
    for (auto& item : zoneItems.GetItems())
        objStore.AddObject(
            item.GetName(),
            BAK::ZoneItemToMeshObject(item, textureStore, pal));

    auto sphere = Sphere{30.0, 12, 6, true};
    objStore.AddObject(
        "combat",
        Graphics::SphereToMeshObject(sphere, glm::vec4{1.0, 0, 0, .7}));
    objStore.AddObject(
        "dialog",
        Graphics::SphereToMeshObject(sphere, glm::vec4{0.0, 1, 0, .7}));
    objStore.AddObject(
        "sound",
        Graphics::SphereToMeshObject(sphere, glm::vec4{1.0, .5, .5, .7}));
    objStore.AddObject(
        "transition",
        Graphics::SphereToMeshObject(sphere, glm::vec4{1.0, 1, 0, .7}));
    objStore.AddObject(
        "trap",
        Graphics::SphereToMeshObject(sphere, glm::vec4{.5, 1, .5, .7}));
    objStore.AddObject(
        "town",
        Graphics::SphereToMeshObject(sphere, glm::vec4{1.0, 0, 1, .7}));
    objStore.AddObject(
        "unknown",
        Graphics::SphereToMeshObject(sphere, glm::vec4{.7, .7, .7, .7}));
    auto clickable = Sphere{3.0, 12, 6, true};
	objStore.AddObject(
        "clickable",
        Graphics::SphereToMeshObject(sphere, glm::vec4{.0, .0, 1.0, .7}));

    auto systems = Systems{};
    std::unordered_map<unsigned, std::shared_ptr<const BAK::Encounter>> encounters{};
    std::unordered_map<unsigned, std::shared_ptr<const BAK::WorldItemInstance>> clickables{};
    for (const auto& world : worlds.GetTiles())
    {
        for (const auto& item : world.GetItems())
        {
            if (item.GetZoneItem().GetVertices().size() > 1)
            {
                logger.Info() << "Item: " << item.GetZoneItem().GetName() << std::endl;
                auto id = systems.GetNextItemId();
                systems.AddRenderable(
                    Renderable{
                        id,
                        objStore.GetObject(item.GetZoneItem().GetName()),
                        item.GetLocation(),
                        item.GetRotation(),
                        glm::vec3{item.GetZoneItem().GetScale()}});

                if (item.GetZoneItem().GetClickable())
                {
                    systems.AddClickable(
                        Intersectable{
                            id,
                            300,
                            item.GetLocation()});
					clickables.emplace(id, std::shared_ptr<const BAK::WorldItemInstance>(&item));
					systems.AddRenderable(
						Renderable{
							id,
							objStore.GetObject("clickable"),
							item.GetLocation(),
							item.GetRotation(),
							glm::vec3{item.GetZoneItem().GetScale()}});
                }
            }
        }

        for (const auto& enc : world.GetEncounters())
        {
            unsigned id = systems.GetNextItemId();
            systems.AddRenderable(
                Renderable{
                    id,
                    objStore.GetObject(BAK::EncounterTypeToString(enc.GetType())),
                    enc.GetLocation(),
                    glm::vec3{0.0},
                    glm::vec3{1.0}});

            systems.AddIntersectable(
                Intersectable{
                    id,
                    3000,
                    enc.GetLocation()});
            encounters.emplace(id, std::shared_ptr<const BAK::Encounter>(&enc));
        }
    }

    if( !glfwInit() )
    {
        logger.Error() << "Failed to initialize GLFW" << std::endl;
        std::exit(1);
    }

    GLFWwindow* window;

    const unsigned antiAliasingSamples = 4;
    glfwWindowHint(GLFW_SAMPLES, antiAliasingSamples);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    unsigned height = 800;
    unsigned width  = 1400;
    /*unsigned height = 1600;
    unsigned width  = 2400;*/

    window = glfwCreateWindow(width, height, "BaK", NULL, NULL);
    if( window == NULL )
    {
        logger.Log(Logging::LogLevel::Error) << "Failed to open GLFW window" << std::endl;
        glfwTerminate();
        std::exit(1);
    }

    glfwMakeContextCurrent(window);

    // Initialize GLEW
    glewExperimental = true; // Needed for core profile
    if (glewInit() != GLEW_OK)
    {
        logger.Log(Logging::LogLevel::Error) << "Failed to initialize GLEW" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Ensure we can capture the escape key being pressed below
    glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);
    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
    
    ImguiWrapper::Initialise(window);
    
    // Dark blue background
    glClearColor(0.15f, 0.31f, 0.36f, 0.0f);

    auto shaderProgram = ShaderProgram{
        "vertex.glsl",
        //"geometry.glsl",
        "fragment.glsl"};

    auto programId = shaderProgram.Compile();

    GLuint VertexArrayID;
    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);
    
    BAK::GLBuffers buffers{};
    buffers.AddBuffer("vertex", 0, 3);
    buffers.AddBuffer("normal", 1, 3);
    buffers.AddBuffer("color", 2, 4);
    buffers.AddBuffer("textureCoord", 3, 3);
    buffers.AddBuffer("textureBlend", 4, 1);

    buffers.LoadBufferDataGL("vertex", GL_ARRAY_BUFFER, objStore.mVertices);
    buffers.LoadBufferDataGL("normal", GL_ARRAY_BUFFER, objStore.mNormals);
    buffers.LoadBufferDataGL("color", GL_ARRAY_BUFFER, objStore.mColors);
    buffers.LoadBufferDataGL("textureCoord", GL_ARRAY_BUFFER, objStore.mTextureCoords);
    buffers.LoadBufferDataGL("textureBlend", GL_ARRAY_BUFFER, objStore.mTextureBlends);

    buffers.LoadBufferDataGL(buffers.mElementBuffer, GL_ELEMENT_ARRAY_BUFFER, objStore.mIndices);
    buffers.BindArraysGL();
    glBindVertexArray(0);

    BAK::TextureBuffer textureBuffer{};
    textureBuffer.LoadTexturesGL(textureStore.GetTextures(), textureStore.GetMaxDim());

    GLuint textureID     = glGetUniformLocation(programId, "texture0");
    GLuint mvpMatrixID   = glGetUniformLocation(programId, "MVP");
    GLuint modelMatrixID = glGetUniformLocation(programId, "M");
    GLuint viewMatrixID  = glGetUniformLocation(programId, "V");

    glm::mat4 viewMatrix{1};
    glm::mat4 modelMatrix{1.0f};
    glm::mat4 MVP{0};

    glm::vec3 lightPos = glm::vec3(0,220,0);
    
    float speed = 400 * 30.0f; // 3 units / second
    float turnSpeed = 30.0f; // 3 units / second

    Camera camera{width, height, worldCenter};
    camera.SetPosition(worldCenter);

    double currentTime;
    double lastTime = 0;
    float deltaTime;

    GLuint lightId = glGetUniformLocation(programId, "lightPosition_worldspace");
    GLuint cameraPositionId = glGetUniformLocation(programId, "cameraPosition_worldspace");

    glfwSetCursorPos(window, width/2, height/2);

    glEnable(GL_MULTISAMPLE);  

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureBuffer.mTextureBuffer);
    glUniform1i(textureID, 0);

    std::shared_ptr<const BAK::Encounter> activeEncounter = nullptr;
    std::shared_ptr<const BAK::WorldItemInstance> activeClickable = nullptr;

	auto pointer = Graphics::Line{
		camera.GetNormalisedPosition(),
		camera.GetNormalisedPosition() + (camera.GetDirection() * 5000.0f)};

	double pointerPosX, pointerPosY;
	Camera pointerEndPoint{width, height, camera.GetPosition()};

    do
    {
        glfwPollEvents();

		glUseProgram(programId);
        glBindVertexArray(VertexArrayID);

        activeEncounter = nullptr;
        currentTime = glfwGetTime();
        deltaTime = float(currentTime - lastTime);
        lastTime = currentTime;
        
		glfwGetCursorPos(window, &pointerPosX, &pointerPosY);

        if (glfwGetKey( window, GLFW_KEY_W) == GLFW_PRESS)
            camera.MoveForward(deltaTime * speed);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.MoveForward(-deltaTime * speed);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.StrafeRight(deltaTime * speed);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.StrafeRight(-deltaTime * speed);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            camera.RotateLeft(deltaTime * (turnSpeed / 12));
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            camera.RotateLeft(-deltaTime * (turnSpeed / 12));
        if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
            camera.RotateVertical(deltaTime * (turnSpeed / 12));
        if (glfwGetKey( window, GLFW_KEY_Y) == GLFW_PRESS)
            camera.RotateVertical(-deltaTime * (turnSpeed / 12));

        if (glfwGetKey( window, GLFW_KEY_P) == GLFW_PRESS)
            lightPos.y += .5;
        if (glfwGetKey( window, GLFW_KEY_L) == GLFW_PRESS)
            lightPos.y -= .5;

        lightPos.x = camera.GetNormalisedPosition().x;
        lightPos.z = camera.GetNormalisedPosition().z;
        
        // Update the camera position and light position
        glUniform3f(lightId, lightPos.x, lightPos.y, lightPos.z);
        glUniform3f(
            cameraPositionId,
            camera.GetNormalisedPosition().x,
            camera.GetNormalisedPosition().y,
            camera.GetNormalisedPosition().z);

        viewMatrix = camera.GetViewMatrix();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (const auto& item : systems.GetRenderables())
        {
			if (glm::distance(camera.GetPosition(), item.GetLocation()) > 128000.0) continue;
            const auto [offset, length] = item.GetObject();
            auto modelMatrix = item.GetModelMatrix();

            MVP = camera.GetProjectionMatrix() * viewMatrix * modelMatrix;

            glUniformMatrix4fv(mvpMatrixID,   1, GL_FALSE, glm::value_ptr(MVP));
            glUniformMatrix4fv(modelMatrixID, 1, GL_FALSE, glm::value_ptr(modelMatrix));
            glUniformMatrix4fv(viewMatrixID,  1, GL_FALSE, glm::value_ptr(viewMatrix));

            glDrawElementsBaseVertex(
                GL_TRIANGLES,
                length,
                GL_UNSIGNED_INT,
                (void*) (offset * sizeof(GLuint)),
                offset
            );
        }
		
		pointerEndPoint.SetAngle(camera.GetAngle());
		pointerEndPoint.SetPosition(camera.GetPosition() + (camera.GetDirection() * 1000.0f));
		pointerEndPoint.StrafeRight((pointerPosX - (width / 2)) * 1.0f);
		pointerEndPoint.StrafeUp(-(pointerPosY - (height/ 2)) * 1.0f);

		pointer.updateLine(
			camera.GetNormalisedPosition() + (camera.GetDirection() * 50.0f),
			pointerEndPoint.GetNormalisedPosition());
		pointer.setMVP(camera.GetProjectionMatrix() * camera.GetViewMatrix());
		pointer.draw();

        auto intersectable = systems.RunIntersection(camera.GetPosition());
        if (intersectable)
            activeEncounter = encounters[*intersectable];

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
			activeClickable = nullptr;

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            auto bestId = systems.RunClickable(
                std::make_pair(
                    camera.GetPosition(), 
                    camera.GetPosition() + (camera.GetDirection() * 3000.0f)));
            if (bestId)
                activeClickable = clickables[*bestId];
        }

        
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        {
            ImGui::Begin("Info");
            std::stringstream ss{};
            ss << "Pos: " << camera.GetPosition() 
                << "\nNPos: " << camera.GetNormalisedPosition()
                << "\nPPos: " << pointerEndPoint.GetNormalisedPosition()
                << "\nTile: " << glm::floor(camera.GetPosition() / glm::vec3{BAK::gTileSize});
            ImGui::Text(ss.str().c_str());
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f 
                / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();

            if (activeEncounter != nullptr)
            {
                ImGui::Begin("Encounter");
                std::stringstream ss{};
                ss << "Encounter: " << activeEncounter->GetType() << " Index: "
                    << activeEncounter->GetIndex();
                ImGui::Text(ss.str().c_str());
                ImGui::End();

                if (activeEncounter->GetType() == BAK::EncounterType::Dialog)
                {
                    ShowDialogGui(
                        activeEncounter->GetIndex(),
                        dialogStore,
                        dialogIndex);
                }
            }

			if (activeClickable != nullptr)
            {
                ImGui::Begin("Clickable");
                std::stringstream ss{};
                ss << "Clickable: " << activeClickable->GetZoneItem().GetName() << " Location: "
                    << activeClickable->GetLocation();
                ImGui::Text(ss.str().c_str());
                ImGui::End();
            }
        }
        ImguiWrapper::Draw(window);
        
        // Swap buffers
        glfwSwapBuffers(window);
    } // Check if the ESC key was pressed or the window was closed
    while (glfwGetKey(window, GLFW_KEY_ESCAPE) != GLFW_PRESS 
        && glfwWindowShouldClose(window) == 0);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);

    glDeleteVertexArrays(1, &VertexArrayID);
    glDeleteProgram(programId);

    ImguiWrapper::Shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

