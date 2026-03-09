/*
* (C) 2025 badasahog. All Rights Reserved
*
* The above copyright notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#undef _CRT_SECURE_NO_WARNINGS

#include <ShellScalingAPI.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <cglm/cglm.h>
#include <cglm/clipspace/persp_rh_zo.h>

#include <stdint.h>
#include <stdio.h>
#include <math.h>

__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#pragma comment(linker, "/DEFAULTLIB:vulkan-1.lib")
#pragma comment(linker, "/DEFAULTLIB:Shcore.lib")

HANDLE ConsoleHandle;

inline void THROW_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			NULL
		);

		if (formattedErrorLength == 0)
			WriteConsoleA(ConsoleHandle, "an error occured, unable to retrieve error message\n", 51, NULL, NULL);
		else
		{
			WriteConsoleA(ConsoleHandle, "an error occured: ", 18, NULL, NULL);
			WriteConsoleW(ConsoleHandle, messageBuffer, formattedErrorLength, NULL, NULL);
			WriteConsoleA(ConsoleHandle, "\n", 1, NULL, NULL);
			LocalFree(messageBuffer);
		}

		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "error code: 0x%X\nlocation:line %i\n", hr, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

void THROW_ON_FAIL_VK_IMPL(VkResult Result, int line)
{
	if (Result < VK_SUCCESS)
	{
		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "Vulkan Error: %i\nlocation:line %i\n", Result, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

#define THROW_ON_FAIL(x) THROW_ON_FAIL_IMPL(x, __LINE__)

#define THROW_ON_FAIL_VK(x) THROW_ON_FAIL_VK_IMPL(x, __LINE__)

#define THROW_ON_FALSE(x) if((x) == FALSE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

#define VALIDATE_HANDLE(x) if((x) == NULL || (x) == INVALID_HANDLE_VALUE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

#define OffsetPointer(x, Offset) ((typeof(x))((char*)x + (Offset)))

#define MAX_CONCURRENT_FRAMES 2
#define MAX_DEVICE_COUNT 16
#define MAX_SURFACE_FORMATS 32
#define MAX_QUEUE_FAMILY_COUNT 16
#define SWAP_CHAIN_MAX_IMAGE_COUNT 8
#define MAX_PRESENT_MODES 8

#define VK_FLAGS_NONE 0

#define WM_INIT (WM_USER + 1)

static const char* const VALIDATION_LAYERS[] = {
	"VK_LAYER_KHRONOS_validation"
};

struct ScratchBuffer
{
	uint64_t DeviceAddress;
	VkBuffer Handle;
	VkDeviceMemory Memory;
};

struct AccelerationStructure
{
	VkAccelerationStructureKHR Handle;
	uint64_t DeviceAddress;
	VkDeviceMemory Memory;
	VkBuffer Buffer;
};

struct StorageImage
{
	VkDeviceMemory Memory;
	VkImage Image;
	VkImageView View;
};

enum SHADER_BINDINGS
{
	SHADER_BINDING_RAYGEN,
	SHADER_BINDING_MISS,
	SHADER_BINDING_HIT,
	SHADER_BINDING_COUNT
};

struct UniformData
{
	mat4 ViewInverse;
	mat4 ProjInverse;
	vec4 LightPos;
	int32_t VertexSize;
};

struct Vertex {
	vec3 Pos;
	vec3 Normal;
	vec2 Padding;
	vec4 Color;
};

struct VulkanSwapChain
{
	VkSurfaceKHR Surface;
	VkFormat ColorFormat;
	VkColorSpaceKHR ColorSpace;
	VkSwapchainKHR SwapChain;
	VkImage Images[SWAP_CHAIN_MAX_IMAGE_COUNT];
	VkImageView ImageViews[SWAP_CHAIN_MAX_IMAGE_COUNT];
	uint32_t QueueNodeIndex;
};

enum CameraType
{
	CAMERA_TYPE_LOOKAT,
	CAMERA_TYPE_FIRST_PERSON
};

struct Camera
{
	enum CameraType CameraType;

	vec3 Rotation;
	vec3 Position;

	struct
	{
		mat4 Perspective;
		mat4 View;
	} Matrices;
};

struct VulkanObjects
{
	struct StorageImage StorageImage;

	struct Camera Camera;

	VkStridedDeviceAddressRegionKHR ShaderBindingTables[SHADER_BINDING_COUNT];

	void *UniformBufferPointers[MAX_CONCURRENT_FRAMES];

	PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;

	VkPipeline Pipeline;
	VkPipelineLayout PipelineLayout;
	VkDescriptorSet DescriptorSets[MAX_CONCURRENT_FRAMES];

	VkPhysicalDevice PhysicalDevice;
	VkDevice Device;
	VkQueue Queue;
	VkFormat DepthFormat;
	VkCommandBuffer DrawCmdBuffers[MAX_CONCURRENT_FRAMES];
	VkRenderPass RenderPass;
	uint32_t SwapChainImageCount;
	VkFramebuffer FrameBuffers[SWAP_CHAIN_MAX_IMAGE_COUNT];
	struct VulkanSwapChain swapChain;

	VkSemaphore PresentCompleteSemaphores[MAX_CONCURRENT_FRAMES];
	VkSemaphore RenderCompleteSemaphores[SWAP_CHAIN_MAX_IMAGE_COUNT];
	VkFence WaitFences[MAX_CONCURRENT_FRAMES];

	VkPhysicalDeviceMemoryProperties MemoryProperties;

	uint32_t QueueFamilyCount;
	VkQueueFamilyProperties QueueFamilyProperties[MAX_QUEUE_FAMILY_COUNT];
	
	VkCommandPool CommandPool;
	
	struct
	{
		uint32_t Graphics;
		uint32_t Compute;
	} QueueFamilyIndices;

	double Timer;

	struct {
		VkImage Image;
		VkDeviceMemory Memory;
		VkImageView View;
	} DepthStencil;
};

uint32_t GetQueueFamilyIndex(struct VulkanObjects *VulkanObjects, VkQueueFlags QueueFlags)
{
	if ((QueueFlags & VK_QUEUE_COMPUTE_BIT) == QueueFlags)
	{
		for (uint32_t i = 0; i < VulkanObjects->QueueFamilyCount; i++)
		{
			if ((VulkanObjects->QueueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && ((VulkanObjects->QueueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
			{
				return i;
			}
		}
	}

	for (uint32_t i = 0; i < VulkanObjects->QueueFamilyCount; i++)
	{
		if ((VulkanObjects->QueueFamilyProperties[i].queueFlags & QueueFlags) == QueueFlags)
		{
			return i;
		}
	}
}

uint32_t GetMemoryType(struct VulkanObjects *VulkanObjects, uint32_t TypeBits, VkMemoryPropertyFlags Properties)
{
	for (uint32_t i = 0; i < VulkanObjects->MemoryProperties.memoryTypeCount; i++)
	{
		if ((TypeBits & 1) == 1)
		{
			if ((VulkanObjects->MemoryProperties.memoryTypes[i].propertyFlags & Properties) == Properties)
			{
				return i;
			}
		}
		TypeBits >>= 1;
	}

	return 0;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessageCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT MessageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData)
{
	char buffer[2048];
	int stringlength;
	if (pCallbackData->pMessageIdName)
	{
		stringlength = _snprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, "[%i][%s]: %s\n", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
	}
	else
	{
		stringlength = _snprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, "[%i]: %s\n", pCallbackData->messageIdNumber, pCallbackData->pMessage);
	}

	WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);
	return VK_FALSE;
}


static const wchar_t* const ApplicationName = L"vulkanExample";

LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND Window, UINT uMsg, WPARAM wParam, LPARAM lParam);

void updateViewMatrix(struct Camera *Camera)
{
	mat4 RotM;
	glm_mat4_identity(RotM);

	glm_rotate(RotM, glm_rad(Camera->Rotation[0]), (vec3) { 1.0f, 0.0f, 0.0f });
	glm_rotate(RotM, glm_rad(Camera->Rotation[1]), (vec3) { 0.0f, 1.0f, 0.0f });
	glm_rotate(RotM, glm_rad(Camera->Rotation[2]), (vec3) { 0.0f, 0.0f, 1.0f });

	mat4 TransM;
	glm_translate_make(TransM, Camera->Position);

	if (Camera->CameraType == CAMERA_TYPE_FIRST_PERSON)
	{
		glm_mat4_mul(RotM, TransM, Camera->Matrices.View);
	}
	else
	{
		glm_mat4_mul(TransM, RotM, Camera->Matrices.View);
	}
}

uint32_t AlignedSize(uint32_t Value, uint32_t Alignment)
{
	return (Value + Alignment - 1) & ~(Alignment - 1);
}

int main()
{
	ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	THROW_ON_FAIL(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE));
	
	HINSTANCE Instance = GetModuleHandleW(NULL);

	HICON Icon = LoadIconW(NULL, IDI_APPLICATION);
	HCURSOR Cursor = LoadCursorW(NULL, IDC_ARROW);

	WNDCLASSEXW WindowClass = { 0 };
	WindowClass.cbSize = sizeof(WNDCLASSEXW);
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	WindowClass.lpfnWndProc = PreInitProc;
	WindowClass.cbClsExtra = 0;
	WindowClass.cbWndExtra = 0;
	WindowClass.hInstance = Instance;
	WindowClass.hIcon = Icon;
	WindowClass.hCursor = Cursor;
	WindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	WindowClass.lpszMenuName = NULL;
	WindowClass.lpszClassName = ApplicationName;
	WindowClass.hIconSm = Icon;

	ATOM WindowClassAtom = RegisterClassExW(&WindowClass);
	if (WindowClassAtom == 0)
		THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()));

	RECT WindowRect = { 0 };
	WindowRect.left = 0;
	WindowRect.top = 0;
	WindowRect.right = 1280;
	WindowRect.bottom = 720;

	AdjustWindowRectEx(&WindowRect, WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, FALSE, WS_EX_APPWINDOW | WS_EX_WINDOWEDGE);

	HWND Window = CreateWindowExW(
		0,
		ApplicationName,
		L"Ray tracing reflections",
		WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		0,
		0,
		WindowRect.right - WindowRect.left,
		WindowRect.bottom - WindowRect.top,
		NULL,
		NULL,
		Instance,
		NULL);

	VALIDATE_HANDLE(Window);

	THROW_ON_FALSE(ShowWindow(Window, SW_SHOW));

	{
		uint32_t x = (GetSystemMetrics(SM_CXSCREEN) - WindowRect.right) / 2;
		uint32_t y = (GetSystemMetrics(SM_CYSCREEN) - WindowRect.bottom) / 2;
		THROW_ON_FALSE(SetWindowPos(Window, 0, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE));
	}

	VkInstance VulkanInstance;

	{
		static const char* const InstanceExtensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
		};

		VkApplicationInfo AppInfo = { 0 };
		AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		AppInfo.pApplicationName = "vulkanExample";
		AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		AppInfo.pEngineName = "No Engine";
		AppInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		AppInfo.apiVersion = VK_API_VERSION_1_1;

#ifdef _DEBUG
		VkDebugUtilsMessengerCreateInfoEXT DebugUtilsMessengerCI = { 0 };
		DebugUtilsMessengerCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		DebugUtilsMessengerCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		DebugUtilsMessengerCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
		DebugUtilsMessengerCI.pfnUserCallback = DebugUtilsMessageCallback;
#endif

		VkInstanceCreateInfo InstanceCreateInfo = { 0 };
		InstanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		InstanceCreateInfo.pApplicationInfo = &AppInfo;
		InstanceCreateInfo.enabledExtensionCount = ARRAYSIZE(InstanceExtensions);
		InstanceCreateInfo.ppEnabledExtensionNames = InstanceExtensions;
#ifdef _DEBUG
		InstanceCreateInfo.pNext = &DebugUtilsMessengerCI;
		InstanceCreateInfo.enabledLayerCount = ARRAYSIZE(VALIDATION_LAYERS);
		InstanceCreateInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif

		THROW_ON_FAIL_VK(vkCreateInstance(&InstanceCreateInfo, NULL, &VulkanInstance));
	}

#ifdef _DEBUG
	VkDebugUtilsMessengerEXT DebugUtilsMessenger;
	{
		PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(VulkanInstance, "vkCreateDebugUtilsMessengerEXT");

		VkDebugUtilsMessengerCreateInfoEXT DebugUtilsMessengerCI = { 0 };
		DebugUtilsMessengerCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		DebugUtilsMessengerCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		DebugUtilsMessengerCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
		DebugUtilsMessengerCI.pfnUserCallback = DebugUtilsMessageCallback;
		THROW_ON_FAIL_VK(vkCreateDebugUtilsMessengerEXT(VulkanInstance, &DebugUtilsMessengerCI, NULL, &DebugUtilsMessenger));
	}
#endif

	struct VulkanObjects VulkanObjects = { 0 };
	{
		uint32_t DeviceCount = 0;
		VkPhysicalDevice Devices[MAX_DEVICE_COUNT];
		THROW_ON_FAIL_VK(vkEnumeratePhysicalDevices(VulkanInstance, &DeviceCount, NULL));
		THROW_ON_FAIL_VK(vkEnumeratePhysicalDevices(VulkanInstance, &DeviceCount, Devices));
		
		VulkanObjects.PhysicalDevice = Devices[0];
	}

	vkGetPhysicalDeviceMemoryProperties(VulkanObjects.PhysicalDevice, &VulkanObjects.MemoryProperties);
	
	vkGetPhysicalDeviceQueueFamilyProperties(VulkanObjects.PhysicalDevice, &VulkanObjects.QueueFamilyCount, NULL);
	vkGetPhysicalDeviceQueueFamilyProperties(VulkanObjects.PhysicalDevice, &VulkanObjects.QueueFamilyCount, VulkanObjects.QueueFamilyProperties);

	{
		int UniqueQueueInfoCount = 0;
		VkDeviceQueueCreateInfo QueueCreateInfos[MAX_QUEUE_FAMILY_COUNT];

		const float DefaultQueuePriority = 0.0f;

		VulkanObjects.QueueFamilyIndices.Graphics = GetQueueFamilyIndex(&VulkanObjects, VK_QUEUE_GRAPHICS_BIT);
		
		{
			VkDeviceQueueCreateInfo QueueInfo = { 0 };
			QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			QueueInfo.queueFamilyIndex = VulkanObjects.QueueFamilyIndices.Graphics;
			QueueInfo.queueCount = 1;
			QueueInfo.pQueuePriorities = &DefaultQueuePriority;
			QueueCreateInfos[UniqueQueueInfoCount] = QueueInfo;
			UniqueQueueInfoCount++;
		}

		VulkanObjects.QueueFamilyIndices.Compute = GetQueueFamilyIndex(&VulkanObjects, VK_QUEUE_COMPUTE_BIT);
		if (VulkanObjects.QueueFamilyIndices.Compute != VulkanObjects.QueueFamilyIndices.Graphics)
		{
			VkDeviceQueueCreateInfo QueueInfo = { 0 };
			QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			QueueInfo.queueFamilyIndex = VulkanObjects.QueueFamilyIndices.Compute;
			QueueInfo.queueCount = 1;
			QueueInfo.pQueuePriorities = &DefaultQueuePriority;
			QueueCreateInfos[UniqueQueueInfoCount] = QueueInfo;
			UniqueQueueInfoCount++;
		}

		const char* const DEVICE_EXTENSIONS[] = {
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
			VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
			VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
			VK_KHR_SPIRV_1_4_EXTENSION_NAME,
			VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		VkPhysicalDeviceBufferDeviceAddressFeatures EnabledBufferDeviceAddresFeatures = { 0 };
		EnabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		EnabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;

		VkPhysicalDeviceRayTracingPipelineFeaturesKHR EnabledRayTracingPipelineFeatures = { 0 };
		EnabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		EnabledRayTracingPipelineFeatures.pNext = &EnabledBufferDeviceAddresFeatures;
		EnabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;

		VkPhysicalDeviceAccelerationStructureFeaturesKHR EnabledAccelerationStructureFeatures = { 0 };
		EnabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		EnabledAccelerationStructureFeatures.pNext = &EnabledRayTracingPipelineFeatures;
		EnabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;

		VkPhysicalDeviceFeatures2 PhysicalDeviceFeatures2 = { 0 };
		PhysicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		PhysicalDeviceFeatures2.pNext = &EnabledAccelerationStructureFeatures;
		PhysicalDeviceFeatures2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
		PhysicalDeviceFeatures2.features.samplerAnisotropy = VK_TRUE;

		VkDeviceCreateInfo DeviceCreateInfo = { 0 };
		DeviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		DeviceCreateInfo.pNext = &PhysicalDeviceFeatures2;
		DeviceCreateInfo.queueCreateInfoCount = UniqueQueueInfoCount;
		DeviceCreateInfo.pQueueCreateInfos = QueueCreateInfos;
		DeviceCreateInfo.enabledExtensionCount = ARRAYSIZE(DEVICE_EXTENSIONS);
		DeviceCreateInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS;
		DeviceCreateInfo.pEnabledFeatures = NULL;
		THROW_ON_FAIL_VK(vkCreateDevice(VulkanObjects.PhysicalDevice, &DeviceCreateInfo, NULL, &VulkanObjects.Device));
	}

	{
		VkCommandPoolCreateInfo CmdPoolInfo = { 0 };
		CmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		CmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		CmdPoolInfo.queueFamilyIndex = VulkanObjects.QueueFamilyIndices.Graphics;
		THROW_ON_FAIL_VK(vkCreateCommandPool(VulkanObjects.Device, &CmdPoolInfo, NULL, &VulkanObjects.CommandPool));
	}

	VulkanObjects.Device = VulkanObjects.Device;

	vkGetDeviceQueue(VulkanObjects.Device, VulkanObjects.QueueFamilyIndices.Graphics, 0, &VulkanObjects.Queue);

	{
		static const VkFormat FormatList[] = {
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D16_UNORM_S8_UINT,
			VK_FORMAT_D16_UNORM
		};

		for (int i = 0; i < ARRAYSIZE(FormatList); i++)
		{
			VkFormatProperties FormatProps;
			vkGetPhysicalDeviceFormatProperties(VulkanObjects.PhysicalDevice, FormatList[i], &FormatProps);
			if (FormatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			{
				VulkanObjects.DepthFormat = FormatList[i];
				break;
			}
		}
	}

	{
		VkWin32SurfaceCreateInfoKHR SurfaceCreateInfo = { 0 };
		SurfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		SurfaceCreateInfo.hinstance = Instance;
		SurfaceCreateInfo.hwnd = Window;
		THROW_ON_FAIL_VK(vkCreateWin32SurfaceKHR(VulkanInstance, &SurfaceCreateInfo, NULL, &VulkanObjects.swapChain.Surface));
	}
	
	{
		uint32_t QueueCount;
		VkQueueFamilyProperties QueueProps[MAX_QUEUE_FAMILY_COUNT];
		vkGetPhysicalDeviceQueueFamilyProperties(VulkanObjects.PhysicalDevice, &QueueCount, NULL);
		vkGetPhysicalDeviceQueueFamilyProperties(VulkanObjects.PhysicalDevice, &QueueCount, QueueProps);

		VkBool32 SupportsPresent[MAX_QUEUE_FAMILY_COUNT];
		for (uint32_t i = 0; i < QueueCount; i++)
		{
			vkGetPhysicalDeviceSurfaceSupportKHR(VulkanObjects.PhysicalDevice, i, VulkanObjects.swapChain.Surface, &SupportsPresent[i]);
		}

		uint32_t GraphicsQueueNodeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < QueueCount; i++)
		{
			if ((QueueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
			{
				if (GraphicsQueueNodeIndex == UINT32_MAX)
				{
					GraphicsQueueNodeIndex = i;
				}

				if (SupportsPresent[i] == VK_TRUE)
				{
					GraphicsQueueNodeIndex = i;
					break;
				}
			}
		}

		VulkanObjects.swapChain.QueueNodeIndex = GraphicsQueueNodeIndex;
	}
	
	{
		uint32_t SurfaceFormatCount;
		VkSurfaceFormatKHR SurfaceFormats[MAX_SURFACE_FORMATS];
		THROW_ON_FAIL_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(VulkanObjects.PhysicalDevice, VulkanObjects.swapChain.Surface, &SurfaceFormatCount, NULL));
		THROW_ON_FAIL_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(VulkanObjects.PhysicalDevice, VulkanObjects.swapChain.Surface, &SurfaceFormatCount, SurfaceFormats));

		VkSurfaceFormatKHR SelectedFormat = SurfaceFormats[0];
		for (int i = 0; i < SurfaceFormatCount; i++)
		{
			if (SurfaceFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
				SurfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				SelectedFormat = SurfaceFormats[i];
				break;
			}
		}

		VulkanObjects.swapChain.ColorFormat = SelectedFormat.format;
		VulkanObjects.swapChain.ColorSpace = SelectedFormat.colorSpace;
	}

	VkCommandPool CommandPool;

	{
		VkCommandPoolCreateInfo CmdPoolInfo = { 0 };
		CmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		CmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		CmdPoolInfo.queueFamilyIndex = VulkanObjects.swapChain.QueueNodeIndex;
		THROW_ON_FAIL_VK(vkCreateCommandPool(VulkanObjects.Device, &CmdPoolInfo, NULL, &CommandPool));
	}

	{
		VkCommandBufferAllocateInfo CmdBufAllocateInfo = { 0 };
		CmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		CmdBufAllocateInfo.commandPool = CommandPool;
		CmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		CmdBufAllocateInfo.commandBufferCount = MAX_CONCURRENT_FRAMES;
		THROW_ON_FAIL_VK(vkAllocateCommandBuffers(VulkanObjects.Device, &CmdBufAllocateInfo, VulkanObjects.DrawCmdBuffers));
	}

	vkDestroyRenderPass(VulkanObjects.Device, VulkanObjects.RenderPass, NULL);

	{
		VkAttachmentDescription Attachments[2] = { 0 };
		Attachments[0].format = VulkanObjects.swapChain.ColorFormat;
		Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		Attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Attachments[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		Attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		Attachments[1].format = VulkanObjects.DepthFormat;
		Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		Attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		Attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference ColorReference = { 0 };
		ColorReference.attachment = 0;
		ColorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference DepthReference = { 0 };
		DepthReference.attachment = 1;
		DepthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription SubpassDescription = { 0 };
		SubpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		SubpassDescription.colorAttachmentCount = 1;
		SubpassDescription.pColorAttachments = &ColorReference;
		SubpassDescription.pDepthStencilAttachment = &DepthReference;

		VkSubpassDependency Dependencies[2] = { 0 };
		Dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		Dependencies[0].dstSubpass = 0;
		Dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		Dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		Dependencies[0].srcAccessMask = VK_ACCESS_NONE_KHR;
		Dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		Dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		Dependencies[1].srcSubpass = 0;
		Dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		Dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		Dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		Dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		Dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		Dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo RenderPassInfo = { 0 };
		RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		RenderPassInfo.attachmentCount = ARRAYSIZE(Attachments);
		RenderPassInfo.pAttachments = Attachments;
		RenderPassInfo.subpassCount = 1;
		RenderPassInfo.pSubpasses = &SubpassDescription;
		RenderPassInfo.dependencyCount = ARRAYSIZE(Dependencies);
		RenderPassInfo.pDependencies = Dependencies;
		THROW_ON_FAIL_VK(vkCreateRenderPass(VulkanObjects.Device, &RenderPassInfo, NULL, &VulkanObjects.RenderPass));
	}

	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkGetBufferDeviceAddressKHR");
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkCmdBuildAccelerationStructuresKHR");
	PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR = (PFN_vkBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkBuildAccelerationStructuresKHR");
	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkCreateAccelerationStructureKHR");
	PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkDestroyAccelerationStructureKHR");
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkGetAccelerationStructureBuildSizesKHR");
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkGetAccelerationStructureDeviceAddressKHR");
	VulkanObjects.vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkCmdTraceRaysKHR");
	PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkGetRayTracingShaderGroupHandlesKHR");
	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(VulkanObjects.Device, "vkCreateRayTracingPipelinesKHR");

	struct
	{
		struct
		{
			int Count;
			VkBuffer Buffer;
			VkDeviceMemory Memory;
		} Vertices;

		struct
		{
			int Count;
			VkBuffer Buffer;
			VkDeviceMemory Memory;
		} Indices;
	} SceneGeometryStruct = { 0 };

	{
		HANDLE SceneGeometryFile = CreateFileW(L"scene.bin", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
		VALIDATE_HANDLE(SceneGeometryFile);

		SIZE_T SceneGeometrySize;
		THROW_ON_FALSE(GetFileSizeEx(SceneGeometryFile, &SceneGeometrySize));

		HANDLE SceneGeometryFileMap = CreateFileMappingW(SceneGeometryFile, NULL, PAGE_READONLY, 0, 0, NULL);
		VALIDATE_HANDLE(SceneGeometryFileMap);

		const void* SceneGeometryFileHead = MapViewOfFile(SceneGeometryFileMap, FILE_MAP_READ, 0, 0, 0);
		const uint32_t* SceneGeometryPtr = SceneGeometryFileHead;

		VkCommandBuffer CopyCmd = { 0 };

		{
			VkCommandBufferAllocateInfo CmdBufAllocateInfo = { 0 };
			CmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			CmdBufAllocateInfo.commandPool = VulkanObjects.CommandPool;
			CmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			CmdBufAllocateInfo.commandBufferCount = 1;
			THROW_ON_FAIL_VK(vkAllocateCommandBuffers(VulkanObjects.Device, &CmdBufAllocateInfo, &CopyCmd));
		}

		{
			VkCommandBufferBeginInfo CmdBufInfo = { 0 };
			CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			THROW_ON_FAIL_VK(vkBeginCommandBuffer(CopyCmd, &CmdBufInfo));
		}

		struct StagingBuffer
		{
			VkBuffer Buffer;
			VkDeviceMemory Memory;
		};

		SceneGeometryStruct.Vertices.Count = *SceneGeometryPtr / sizeof(struct Vertex);
		const size_t VertexBufferSize = *SceneGeometryPtr;
		struct StagingBuffer VertexStaging = { 0 };
		SceneGeometryPtr++;

		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			BufferCreateInfo.size = VertexBufferSize;
			BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &SceneGeometryStruct.Vertices.Buffer));
		}

		{
			VkMemoryRequirements MemReqs;
			vkGetBufferMemoryRequirements(VulkanObjects.Device, SceneGeometryStruct.Vertices.Buffer, &MemReqs);

			VkMemoryAllocateFlagsInfoKHR AllocFlagsInfo = { 0 };
			AllocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
			AllocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

			VkMemoryAllocateInfo MemAllocInfo = { 0 };
			MemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemAllocInfo.pNext = &AllocFlagsInfo;
			MemAllocInfo.allocationSize = MemReqs.size;
			MemAllocInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemAllocInfo, NULL, &SceneGeometryStruct.Vertices.Memory));
		}

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, SceneGeometryStruct.Vertices.Buffer, SceneGeometryStruct.Vertices.Memory, 0));

		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			BufferCreateInfo.size = VertexBufferSize;
			BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &VertexStaging.Buffer));
		}

		{
			VkMemoryRequirements MemReqs;
			vkGetBufferMemoryRequirements(VulkanObjects.Device, VertexStaging.Buffer, &MemReqs);

			VkMemoryAllocateInfo MemAllocInfo = { 0 };
			MemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemAllocInfo.allocationSize = MemReqs.size;
			MemAllocInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemAllocInfo, NULL, &VertexStaging.Memory));
		}

		{
			void* Mapped;
			THROW_ON_FAIL_VK(vkMapMemory(VulkanObjects.Device, VertexStaging.Memory, 0, VertexBufferSize, 0, &Mapped));
			memcpy(Mapped, SceneGeometryPtr, VertexBufferSize);
			vkUnmapMemory(VulkanObjects.Device, VertexStaging.Memory);
		}

		SceneGeometryPtr = OffsetPointer(SceneGeometryPtr, VertexBufferSize);

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, VertexStaging.Buffer, VertexStaging.Memory, 0));

		{
			VkBufferCopy CopyRegion = { 0 };
			CopyRegion.size = VertexBufferSize;
			vkCmdCopyBuffer(CopyCmd, VertexStaging.Buffer, SceneGeometryStruct.Vertices.Buffer, 1, &CopyRegion);
		}

		SceneGeometryStruct.Indices.Count = *SceneGeometryPtr / sizeof(uint32_t);
		const size_t IndexBufferSize = *SceneGeometryPtr;
		SceneGeometryPtr++;

		struct StagingBuffer IndexStaging = { 0 };
		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			BufferCreateInfo.size = IndexBufferSize;
			BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &SceneGeometryStruct.Indices.Buffer));
		}

		{
			VkMemoryRequirements MemReqs;
			vkGetBufferMemoryRequirements(VulkanObjects.Device, SceneGeometryStruct.Indices.Buffer, &MemReqs);

			VkMemoryAllocateFlagsInfoKHR AllocFlagsInfo = { 0 };
			AllocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
			AllocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

			VkMemoryAllocateInfo MemAllocInfo = { 0 };
			MemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemAllocInfo.pNext = &AllocFlagsInfo;
			MemAllocInfo.allocationSize = MemReqs.size;
			MemAllocInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemAllocInfo, NULL, &SceneGeometryStruct.Indices.Memory));
		}

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, SceneGeometryStruct.Indices.Buffer, SceneGeometryStruct.Indices.Memory, 0));

		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			BufferCreateInfo.size = IndexBufferSize;
			BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &IndexStaging.Buffer));
		}

		{
			VkMemoryRequirements MemReqs;
			vkGetBufferMemoryRequirements(VulkanObjects.Device, IndexStaging.Buffer, &MemReqs);

			VkMemoryAllocateInfo MemAllocInfo = { 0 };
			MemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemAllocInfo.allocationSize = MemReqs.size;
			MemAllocInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemAllocInfo, NULL, &IndexStaging.Memory));
		}

		{
			void* Mapped;
			THROW_ON_FAIL_VK(vkMapMemory(VulkanObjects.Device, IndexStaging.Memory, 0, IndexBufferSize, 0, &Mapped));
			memcpy(Mapped, SceneGeometryPtr, IndexBufferSize);
			vkUnmapMemory(VulkanObjects.Device, IndexStaging.Memory);
		}

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, IndexStaging.Buffer, IndexStaging.Memory, 0));

		{
			VkBufferCopy CopyRegion = { 0 };
			CopyRegion.size = IndexBufferSize;
			vkCmdCopyBuffer(CopyCmd, IndexStaging.Buffer, SceneGeometryStruct.Indices.Buffer, 1, &CopyRegion);
		}

		THROW_ON_FAIL_VK(vkEndCommandBuffer(CopyCmd));

		{
			VkFence Fence;

			{
				VkFenceCreateInfo FenceInfo = { 0 };
				FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				FenceInfo.flags = VK_FLAGS_NONE;
				THROW_ON_FAIL_VK(vkCreateFence(VulkanObjects.Device, &FenceInfo, NULL, &Fence));
			}

			{
				VkSubmitInfo SubmitInfo = { 0 };
				SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				SubmitInfo.commandBufferCount = 1;
				SubmitInfo.pCommandBuffers = &CopyCmd;
				THROW_ON_FAIL_VK(vkQueueSubmit(VulkanObjects.Queue, 1, &SubmitInfo, Fence));
			}

			THROW_ON_FAIL_VK(vkWaitForFences(VulkanObjects.Device, 1, &Fence, VK_TRUE, UINT64_MAX));
			vkDestroyFence(VulkanObjects.Device, Fence, NULL);
		}

		vkFreeCommandBuffers(VulkanObjects.Device, VulkanObjects.CommandPool, 1, &CopyCmd);

		vkDestroyBuffer(VulkanObjects.Device, VertexStaging.Buffer, NULL);
		vkFreeMemory(VulkanObjects.Device, VertexStaging.Memory, NULL);

		vkDestroyBuffer(VulkanObjects.Device, IndexStaging.Buffer, NULL);
		vkFreeMemory(VulkanObjects.Device, IndexStaging.Memory, NULL);

		THROW_ON_FALSE(UnmapViewOfFile(SceneGeometryFileHead));
		THROW_ON_FALSE(CloseHandle(SceneGeometryFileMap));
		THROW_ON_FALSE(CloseHandle(SceneGeometryFile));
	}
	
	struct AccelerationStructure BottomLevelAS;
	struct AccelerationStructure TopLevelAS;

	{
		VkDeviceAddress VertexBufferDeviceAddress;
		
		{
			VkBufferDeviceAddressInfoKHR BufferDeviceAI = { 0 };
			BufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			BufferDeviceAI.buffer = SceneGeometryStruct.Vertices.Buffer;
			VertexBufferDeviceAddress = vkGetBufferDeviceAddressKHR(VulkanObjects.Device, &BufferDeviceAI);
		}

		VkDeviceAddress IndexBufferDeviceAddress;

		{
			VkBufferDeviceAddressInfoKHR BufferDeviceAI = { 0 };
			BufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			BufferDeviceAI.buffer = SceneGeometryStruct.Indices.Buffer;
			IndexBufferDeviceAddress = vkGetBufferDeviceAddressKHR(VulkanObjects.Device, &BufferDeviceAI);
		}

		VkAccelerationStructureGeometryKHR AccelerationStructureGeometry = { 0 };
		AccelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		AccelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		AccelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		AccelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		AccelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		AccelerationStructureGeometry.geometry.triangles.vertexData.deviceAddress = VertexBufferDeviceAddress;
		AccelerationStructureGeometry.geometry.triangles.maxVertex = SceneGeometryStruct.Vertices.Count - 1;
		AccelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(struct Vertex);
		AccelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		AccelerationStructureGeometry.geometry.triangles.indexData.deviceAddress = IndexBufferDeviceAddress;
		AccelerationStructureGeometry.geometry.triangles.transformData.deviceAddress = 0;
		AccelerationStructureGeometry.geometry.triangles.transformData.hostAddress = NULL;

		VkAccelerationStructureBuildGeometryInfoKHR AccelerationStructureBuildGeometryInfo = { 0 };
		AccelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		AccelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		AccelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		AccelerationStructureBuildGeometryInfo.geometryCount = 1;
		AccelerationStructureBuildGeometryInfo.pGeometries = &AccelerationStructureGeometry;

		uint32_t numTriangles = SceneGeometryStruct.Indices.Count / 3;
	
		VkAccelerationStructureBuildSizesInfoKHR AccelerationStructureBuildSizesInfo = { 0 };
		AccelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

		vkGetAccelerationStructureBuildSizesKHR(
			VulkanObjects.Device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&AccelerationStructureBuildGeometryInfo,
			&numTriangles,
			&AccelerationStructureBuildSizesInfo);

		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.size = AccelerationStructureBuildSizesInfo.accelerationStructureSize;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &BottomLevelAS.Buffer));
		}

		{
			VkMemoryRequirements MemoryRequirements = { 0 };
			vkGetBufferMemoryRequirements(VulkanObjects.Device, BottomLevelAS.Buffer, &MemoryRequirements);

			VkMemoryAllocateFlagsInfo MemoryAllocateFlagsInfo = { 0 };
			MemoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
			MemoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

			VkMemoryAllocateInfo MemoryAllocateInfo = { 0 };
			MemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemoryAllocateInfo.pNext = &MemoryAllocateFlagsInfo;
			MemoryAllocateInfo.allocationSize = MemoryRequirements.size;
			MemoryAllocateInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemoryAllocateInfo, NULL, &BottomLevelAS.Memory));
		}

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, BottomLevelAS.Buffer, BottomLevelAS.Memory, 0));

		{
			VkAccelerationStructureCreateInfoKHR AccelerationStructureCreateInfo = { 0 };
			AccelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
			AccelerationStructureCreateInfo.buffer = BottomLevelAS.Buffer;
			AccelerationStructureCreateInfo.size = AccelerationStructureBuildSizesInfo.accelerationStructureSize;
			AccelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			vkCreateAccelerationStructureKHR(VulkanObjects.Device, &AccelerationStructureCreateInfo, NULL, &BottomLevelAS.Handle);
		}

		{
			VkAccelerationStructureDeviceAddressInfoKHR AccelerationDeviceAddressInfo = { 0 };
			AccelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
			AccelerationDeviceAddressInfo.accelerationStructure = BottomLevelAS.Handle;
			BottomLevelAS.DeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(VulkanObjects.Device, &AccelerationDeviceAddressInfo);
		}

		VkBuffer ScratchBufferHandle;

		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.flags = 0;
			BufferCreateInfo.size = AccelerationStructureBuildSizesInfo.buildScratchSize;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &ScratchBufferHandle));
		}

		VkDeviceMemory ScratchBufferMemory;

		{
			VkMemoryAllocateFlagsInfo MemoryAllocateFlagsInfo = { 0 };
			MemoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
			MemoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

			VkMemoryRequirements MemoryRequirements = { 0 };
			vkGetBufferMemoryRequirements(VulkanObjects.Device, ScratchBufferHandle, &MemoryRequirements);

			VkMemoryAllocateInfo MemoryAllocateInfo = { 0 };
			MemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemoryAllocateInfo.pNext = &MemoryAllocateFlagsInfo;
			MemoryAllocateInfo.allocationSize = MemoryRequirements.size;
			MemoryAllocateInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemoryAllocateInfo, NULL, &ScratchBufferMemory));
		}

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, ScratchBufferHandle, ScratchBufferMemory, 0));

		uint64_t ScratchBufferDeviceAddress = 0;

		{
			VkBufferDeviceAddressInfoKHR BufferDeviceAddresInfo = { 0 };
			BufferDeviceAddresInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			BufferDeviceAddresInfo.buffer = ScratchBufferHandle;
			ScratchBufferDeviceAddress = vkGetBufferDeviceAddressKHR(VulkanObjects.Device, &BufferDeviceAddresInfo);
		}
		
		VkAccelerationStructureBuildGeometryInfoKHR AccelerationBuildGeometryInfo = { 0 };
		AccelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		AccelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		AccelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		AccelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		AccelerationBuildGeometryInfo.dstAccelerationStructure = BottomLevelAS.Handle;
		AccelerationBuildGeometryInfo.geometryCount = 1;
		AccelerationBuildGeometryInfo.pGeometries = &AccelerationStructureGeometry;
		AccelerationBuildGeometryInfo.scratchData.deviceAddress = ScratchBufferDeviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR AccelerationStructureBuildRangeInfo = { 0 };
		AccelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
		AccelerationStructureBuildRangeInfo.primitiveOffset = 0;
		AccelerationStructureBuildRangeInfo.firstVertex = 0;
		AccelerationStructureBuildRangeInfo.transformOffset = 0;
		VkAccelerationStructureBuildRangeInfoKHR* AccelerationBuildStructureRangeInfos = &AccelerationStructureBuildRangeInfo;

		VkCommandBuffer CommandBuffer;

		{
			VkCommandBufferAllocateInfo CmdBufAllocateInfo = { 0 };
			CmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			CmdBufAllocateInfo.commandPool = VulkanObjects.CommandPool;
			CmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			CmdBufAllocateInfo.commandBufferCount = 1;
			THROW_ON_FAIL_VK(vkAllocateCommandBuffers(VulkanObjects.Device, &CmdBufAllocateInfo, &CommandBuffer));
		}

		{
			VkCommandBufferBeginInfo CmdBufInfo = { 0 };
			CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			THROW_ON_FAIL_VK(vkBeginCommandBuffer(CommandBuffer, &CmdBufInfo));
		}

		vkCmdBuildAccelerationStructuresKHR(
			CommandBuffer,
			1,
			&AccelerationBuildGeometryInfo,
			&AccelerationBuildStructureRangeInfos);

		THROW_ON_FAIL_VK(vkEndCommandBuffer(CommandBuffer));

		{
			VkFence Fence;

			{
				VkFenceCreateInfo FenceInfo = { 0 };
				FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				FenceInfo.flags = VK_FLAGS_NONE;
				THROW_ON_FAIL_VK(vkCreateFence(VulkanObjects.Device, &FenceInfo, NULL, &Fence));
			}

			{
				VkSubmitInfo SubmitInfo = { 0 };
				SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				SubmitInfo.commandBufferCount = 1;
				SubmitInfo.pCommandBuffers = &CommandBuffer;
				THROW_ON_FAIL_VK(vkQueueSubmit(VulkanObjects.Queue, 1, &SubmitInfo, Fence));
			}

			THROW_ON_FAIL_VK(vkWaitForFences(VulkanObjects.Device, 1, &Fence, VK_TRUE, UINT64_MAX));
			vkDestroyFence(VulkanObjects.Device, Fence, NULL);
		}

		vkFreeCommandBuffers(VulkanObjects.Device, VulkanObjects.CommandPool, 1, &CommandBuffer);

		vkFreeMemory(VulkanObjects.Device, ScratchBufferMemory, NULL);

		vkDestroyBuffer(VulkanObjects.Device, ScratchBufferHandle, NULL);
	}

	{
		struct
		{
			VkBuffer Buffer;
			VkDeviceMemory Memory;
		} InstancesBuffer = { 0 };

		{
			{
				VkBufferCreateInfo BufferCreateInfo = { 0 };
				BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				BufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
				BufferCreateInfo.size = sizeof(VkAccelerationStructureInstanceKHR);
				THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &InstancesBuffer.Buffer));
			}

			{
				VkMemoryAllocateFlagsInfoKHR AllocFlagsInfo = { 0 };
				AllocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
				AllocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

				VkMemoryRequirements MemReqs;
				vkGetBufferMemoryRequirements(VulkanObjects.Device, InstancesBuffer.Buffer, &MemReqs);

				VkMemoryAllocateInfo MemAlloc = { 0 };
				MemAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				MemAlloc.pNext = &AllocFlagsInfo;
				MemAlloc.allocationSize = MemReqs.size;
				MemAlloc.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemAlloc, NULL, &InstancesBuffer.Memory));
			}

			{
				void* Mapped;
				THROW_ON_FAIL_VK(vkMapMemory(VulkanObjects.Device, InstancesBuffer.Memory, 0, VK_WHOLE_SIZE, 0, &Mapped));

				VkTransformMatrixKHR TransformMatrix = {
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f };

				VkAccelerationStructureInstanceKHR Instance = { 0 };
				Instance.transform = TransformMatrix;
				Instance.instanceCustomIndex = 0;
				Instance.mask = 0xFF;
				Instance.instanceShaderBindingTableRecordOffset = 0;
				Instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
				Instance.accelerationStructureReference = BottomLevelAS.DeviceAddress;

				memcpy(Mapped, &Instance, sizeof(VkAccelerationStructureInstanceKHR));
			}

			vkUnmapMemory(VulkanObjects.Device, InstancesBuffer.Memory);

			THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, InstancesBuffer.Buffer, InstancesBuffer.Memory, 0));
		}

		VkDeviceOrHostAddressConstKHR InstanceDataDeviceAddress = { 0 };

		{
			VkBufferDeviceAddressInfoKHR BufferDeviceAI = { 0 };
			BufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			BufferDeviceAI.buffer = InstancesBuffer.Buffer;
			InstanceDataDeviceAddress.deviceAddress = vkGetBufferDeviceAddressKHR(VulkanObjects.Device, &BufferDeviceAI);
		}

		VkAccelerationStructureGeometryKHR AccelerationStructureGeometry = { 0 };
		AccelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		AccelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		AccelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		AccelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
		AccelerationStructureGeometry.geometry.instances.data = InstanceDataDeviceAddress;
		AccelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

		VkAccelerationStructureBuildGeometryInfoKHR AccelerationStructureBuildGeometryInfo = { 0 };
		AccelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		AccelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		AccelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		AccelerationStructureBuildGeometryInfo.geometryCount = 1;
		AccelerationStructureBuildGeometryInfo.pGeometries = &AccelerationStructureGeometry;

		uint32_t PrimitiveCount = 1;

		VkAccelerationStructureBuildSizesInfoKHR AccelerationStructureBuildSizesInfo = { 0 };
		AccelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

		vkGetAccelerationStructureBuildSizesKHR(
			VulkanObjects.Device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&AccelerationStructureBuildGeometryInfo,
			&PrimitiveCount,
			&AccelerationStructureBuildSizesInfo);

		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.size = AccelerationStructureBuildSizesInfo.accelerationStructureSize;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &TopLevelAS.Buffer));
		}

		{
			VkMemoryRequirements MemoryRequirements;
			vkGetBufferMemoryRequirements(VulkanObjects.Device, TopLevelAS.Buffer, &MemoryRequirements);

			VkMemoryAllocateFlagsInfo MemoryAllocateFlagsInfo = { 0 };
			MemoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
			MemoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

			VkMemoryAllocateInfo MemoryAllocateInfo = { 0 };
			MemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemoryAllocateInfo.pNext = &MemoryAllocateFlagsInfo;
			MemoryAllocateInfo.allocationSize = MemoryRequirements.size;
			MemoryAllocateInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemoryAllocateInfo, NULL, &TopLevelAS.Memory));
		}

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, TopLevelAS.Buffer, TopLevelAS.Memory, 0));

		{
			VkAccelerationStructureCreateInfoKHR AccelerationStructureCreateInfo = { 0 };
			AccelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
			AccelerationStructureCreateInfo.buffer = TopLevelAS.Buffer;
			AccelerationStructureCreateInfo.size = AccelerationStructureBuildSizesInfo.accelerationStructureSize;
			AccelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			vkCreateAccelerationStructureKHR(VulkanObjects.Device, &AccelerationStructureCreateInfo, NULL, &TopLevelAS.Handle);
		}

		{
			VkAccelerationStructureDeviceAddressInfoKHR AccelerationDeviceAddressInfo = { 0 };
			AccelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
			AccelerationDeviceAddressInfo.accelerationStructure = TopLevelAS.Handle;
			TopLevelAS.DeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(VulkanObjects.Device, &AccelerationDeviceAddressInfo);
		}

		struct ScratchBuffer ScratchBuffer = { 0 };

		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.flags = 0;
			BufferCreateInfo.size = AccelerationStructureBuildSizesInfo.buildScratchSize;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &ScratchBuffer.Handle));
		}

		{
			VkMemoryRequirements MemoryRequirements = { 0 };
			vkGetBufferMemoryRequirements(VulkanObjects.Device, ScratchBuffer.Handle, &MemoryRequirements);

			VkMemoryAllocateFlagsInfo MemoryAllocateFlagsInfo = { 0 };
			MemoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
			MemoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

			VkMemoryAllocateInfo MemoryAllocateInfo = { 0 };
			MemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemoryAllocateInfo.pNext = &MemoryAllocateFlagsInfo;
			MemoryAllocateInfo.allocationSize = MemoryRequirements.size;
			MemoryAllocateInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemoryAllocateInfo, NULL, &ScratchBuffer.Memory));
		}

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, ScratchBuffer.Handle, ScratchBuffer.Memory, 0));

		{
			VkBufferDeviceAddressInfoKHR BufferDeviceAddresInfo = { 0 };
			BufferDeviceAddresInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			BufferDeviceAddresInfo.buffer = ScratchBuffer.Handle;
			ScratchBuffer.DeviceAddress = vkGetBufferDeviceAddressKHR(VulkanObjects.Device, &BufferDeviceAddresInfo);
		}

		VkAccelerationStructureBuildGeometryInfoKHR AccelerationBuildGeometryInfo = { 0 };
		AccelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		AccelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		AccelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		AccelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		AccelerationBuildGeometryInfo.dstAccelerationStructure = TopLevelAS.Handle;
		AccelerationBuildGeometryInfo.geometryCount = 1;
		AccelerationBuildGeometryInfo.pGeometries = &AccelerationStructureGeometry;
		AccelerationBuildGeometryInfo.scratchData.deviceAddress = ScratchBuffer.DeviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR AccelerationStructureBuildRangeInfo = { 0 };
		AccelerationStructureBuildRangeInfo.primitiveCount = 1;
		AccelerationStructureBuildRangeInfo.primitiveOffset = 0;
		AccelerationStructureBuildRangeInfo.firstVertex = 0;
		AccelerationStructureBuildRangeInfo.transformOffset = 0;

		VkAccelerationStructureBuildRangeInfoKHR* AccelerationBuildStructureRangeInfos = &AccelerationStructureBuildRangeInfo;

		VkCommandBuffer CommandBuffer = { 0 };
		
		{
			VkCommandBufferAllocateInfo CmdBufAllocateInfo = { 0 };
			CmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			CmdBufAllocateInfo.commandPool = VulkanObjects.CommandPool;
			CmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			CmdBufAllocateInfo.commandBufferCount = 1;
			THROW_ON_FAIL_VK(vkAllocateCommandBuffers(VulkanObjects.Device, &CmdBufAllocateInfo, &CommandBuffer));
		}

		{
			VkCommandBufferBeginInfo CmdBufInfo = { 0 };
			CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			THROW_ON_FAIL_VK(vkBeginCommandBuffer(CommandBuffer, &CmdBufInfo));
		}

		vkCmdBuildAccelerationStructuresKHR(
			CommandBuffer,
			1,
			&AccelerationBuildGeometryInfo,
			&AccelerationBuildStructureRangeInfos);

		THROW_ON_FAIL_VK(vkEndCommandBuffer(CommandBuffer));

		VkFence Fence;

		{
			VkFenceCreateInfo FenceInfo = { 0 };
			FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			FenceInfo.flags = VK_FLAGS_NONE;
			THROW_ON_FAIL_VK(vkCreateFence(VulkanObjects.Device, &FenceInfo, NULL, &Fence));
		}

		{
			VkSubmitInfo SubmitInfo = { 0 };
			SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			SubmitInfo.commandBufferCount = 1;
			SubmitInfo.pCommandBuffers = &CommandBuffer;
			THROW_ON_FAIL_VK(vkQueueSubmit(VulkanObjects.Queue, 1, &SubmitInfo, Fence));
		}

		THROW_ON_FAIL_VK(vkWaitForFences(VulkanObjects.Device, 1, &Fence, VK_TRUE, UINT64_MAX));
		vkDestroyFence(VulkanObjects.Device, Fence, NULL);
		vkFreeCommandBuffers(VulkanObjects.Device, VulkanObjects.CommandPool, 1, &CommandBuffer);

		vkFreeMemory(VulkanObjects.Device, ScratchBuffer.Memory, NULL);
		vkDestroyBuffer(VulkanObjects.Device, ScratchBuffer.Handle, NULL);

		vkFreeMemory(VulkanObjects.Device, InstancesBuffer.Memory, NULL);
		vkDestroyBuffer(VulkanObjects.Device, InstancesBuffer.Buffer, NULL);
	}

	struct UniformBuffer
	{
		VkBuffer UnifromBuffer;
		VkDeviceMemory UnifromBufferMemory;
		VkDescriptorBufferInfo UnifromBufferDescriptor;
	};

	struct UniformBuffer UniformBuffers[MAX_CONCURRENT_FRAMES];

	for (int i = 0; i < ARRAYSIZE(UniformBuffers); i++)
	{
		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.size = sizeof(struct UniformData);
			BufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &UniformBuffers[i].UnifromBuffer));
		}

		{
			VkMemoryRequirements MemReqs;
			vkGetBufferMemoryRequirements(VulkanObjects.Device, UniformBuffers[i].UnifromBuffer, &MemReqs);

			VkMemoryAllocateInfo MemAlloc = { 0 };
			MemAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemAlloc.allocationSize = MemReqs.size;
			MemAlloc.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemAlloc, NULL, &UniformBuffers[i].UnifromBufferMemory));
		}

		UniformBuffers[i].UnifromBufferDescriptor.offset = 0;
		UniformBuffers[i].UnifromBufferDescriptor.buffer = UniformBuffers[i].UnifromBuffer;
		UniformBuffers[i].UnifromBufferDescriptor.range = VK_WHOLE_SIZE;

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, UniformBuffers[i].UnifromBuffer, UniformBuffers[i].UnifromBufferMemory, 0));

		THROW_ON_FAIL_VK(vkMapMemory(
			VulkanObjects.Device,
			UniformBuffers[i].UnifromBufferMemory,
			0,
			VK_WHOLE_SIZE,
			0,
			&VulkanObjects.UniformBufferPointers[i]));
	}

	VkDescriptorSetLayout DescriptorSetLayout;

	{
		VkDescriptorSetLayoutBinding SetLayoutBindings[5] = { 0 };
		// Binding 0: Acceleration structure
		SetLayoutBindings[0].binding = 0;
		SetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		SetLayoutBindings[0].descriptorCount = 1;
		SetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

		// Binding 1: Storage image
		SetLayoutBindings[1].binding = 1;
		SetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		SetLayoutBindings[1].descriptorCount = 1;
		SetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

		// Binding 2: Uniform buffer
		SetLayoutBindings[2].binding = 2;
		SetLayoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		SetLayoutBindings[2].descriptorCount = 1;
		SetLayoutBindings[2].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;

		// Binding 3: Vertex buffer 
		SetLayoutBindings[3].binding = 3;
		SetLayoutBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		SetLayoutBindings[3].descriptorCount = 1;
		SetLayoutBindings[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

		// Binding 4: Index buffer
		SetLayoutBindings[4].binding = 4;
		SetLayoutBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		SetLayoutBindings[4].descriptorCount = 1;
		SetLayoutBindings[4].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

		{
			VkDescriptorSetLayoutCreateInfo DescriptorSetLayoutCI = { 0 };
			DescriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			DescriptorSetLayoutCI.pBindings = SetLayoutBindings;
			DescriptorSetLayoutCI.bindingCount = ARRAYSIZE(SetLayoutBindings);
			THROW_ON_FAIL_VK(vkCreateDescriptorSetLayout(VulkanObjects.Device, &DescriptorSetLayoutCI, NULL, &DescriptorSetLayout));
		}
	}

	{
		VkPipelineLayoutCreateInfo PipelineLayoutCI = { 0 };
		PipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		PipelineLayoutCI.setLayoutCount = 1;
		PipelineLayoutCI.pSetLayouts = &DescriptorSetLayout;
		THROW_ON_FAIL_VK(vkCreatePipelineLayout(VulkanObjects.Device, &PipelineLayoutCI, NULL, &VulkanObjects.PipelineLayout));
	}

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR  RayTracingPipelineProperties = { 0 };
	RayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

	{
		VkPhysicalDeviceProperties2 DeviceProperties2 = { 0 };
		DeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		DeviceProperties2.pNext = &RayTracingPipelineProperties;
		vkGetPhysicalDeviceProperties2(VulkanObjects.PhysicalDevice, &DeviceProperties2);
	}

	{
		VkSpecializationMapEntry SpecializationMapEntry = { 0 };
		SpecializationMapEntry.constantID = 0;
		SpecializationMapEntry.offset = 0;
		SpecializationMapEntry.size = sizeof(uint32_t);

		uint32_t MaxRecursion = 4;
		VkSpecializationInfo SpecializationInfo = { 0 };
		SpecializationInfo.mapEntryCount = 1;
		SpecializationInfo.pMapEntries = &SpecializationMapEntry;
		SpecializationInfo.dataSize = sizeof(MaxRecursion);
		SpecializationInfo.pData = &MaxRecursion;
		
		VkShaderModule RayGenShaderModule;
		
		{
			HANDLE RayGenShaderFile = CreateFileW(L"raygen.spv", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
			VALIDATE_HANDLE(RayGenShaderFile);

			SIZE_T RayGenShaderSize;
			THROW_ON_FALSE(GetFileSizeEx(RayGenShaderFile, &RayGenShaderSize));

			HANDLE RayGenShaderFileMap = CreateFileMappingW(RayGenShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
			VALIDATE_HANDLE(RayGenShaderFileMap);

			const void* RayGenShaderBytecode = MapViewOfFile(RayGenShaderFileMap, FILE_MAP_READ, 0, 0, 0);

			{
				VkShaderModuleCreateInfo ModuleCreateInfo = { 0 };
				ModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				ModuleCreateInfo.codeSize = RayGenShaderSize;
				ModuleCreateInfo.pCode = RayGenShaderBytecode;
				THROW_ON_FAIL_VK(vkCreateShaderModule(VulkanObjects.Device, &ModuleCreateInfo, NULL, &RayGenShaderModule));
			}

			THROW_ON_FALSE(UnmapViewOfFile(RayGenShaderBytecode));
			THROW_ON_FALSE(CloseHandle(RayGenShaderFileMap));
			THROW_ON_FALSE(CloseHandle(RayGenShaderFile));
		}
		
		VkShaderModule RayMissShaderModule;
		
		{
			HANDLE RayMissShaderFile = CreateFileW(L"miss.spv", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
			VALIDATE_HANDLE(RayMissShaderFile);

			SIZE_T RayMissShaderSize;
			THROW_ON_FALSE(GetFileSizeEx(RayMissShaderFile, &RayMissShaderSize));

			HANDLE RayMissShaderFileMap = CreateFileMappingW(RayMissShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
			VALIDATE_HANDLE(RayMissShaderFileMap);

			const void* RayMissShaderBytecode = MapViewOfFile(RayMissShaderFileMap, FILE_MAP_READ, 0, 0, 0);

			{
				VkShaderModuleCreateInfo ModuleCreateInfo = { 0 };
				ModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				ModuleCreateInfo.codeSize = RayMissShaderSize;
				ModuleCreateInfo.pCode = RayMissShaderBytecode;
				THROW_ON_FAIL_VK(vkCreateShaderModule(VulkanObjects.Device, &ModuleCreateInfo, NULL, &RayMissShaderModule));
			}

			THROW_ON_FALSE(UnmapViewOfFile(RayMissShaderBytecode));
			THROW_ON_FALSE(CloseHandle(RayMissShaderFileMap));
			THROW_ON_FALSE(CloseHandle(RayMissShaderFile));
		}
		
		VkShaderModule ClosestHitShaderModule;

		{
			HANDLE ClosestHitShaderFile = CreateFileW(L"closesthit.spv", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
			VALIDATE_HANDLE(ClosestHitShaderFile);

			SIZE_T ClosestHitShaderSize;
			THROW_ON_FALSE(GetFileSizeEx(ClosestHitShaderFile, &ClosestHitShaderSize));

			HANDLE ClosestHitShaderFileMap = CreateFileMappingW(ClosestHitShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
			VALIDATE_HANDLE(ClosestHitShaderFileMap);

			const void* ClosestHitShaderBytecode = MapViewOfFile(ClosestHitShaderFileMap, FILE_MAP_READ, 0, 0, 0);

			{
				VkShaderModuleCreateInfo ModuleCreateInfo = { 0 };
				ModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				ModuleCreateInfo.codeSize = ClosestHitShaderSize;
				ModuleCreateInfo.pCode = ClosestHitShaderBytecode;
				THROW_ON_FAIL_VK(vkCreateShaderModule(VulkanObjects.Device, &ModuleCreateInfo, NULL, &ClosestHitShaderModule));
			}

			THROW_ON_FALSE(UnmapViewOfFile(ClosestHitShaderBytecode));
			THROW_ON_FALSE(CloseHandle(ClosestHitShaderFileMap));
			THROW_ON_FALSE(CloseHandle(ClosestHitShaderFile));
		}

		VkPipelineShaderStageCreateInfo ShaderStages[3] = { 0 };
		ShaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ShaderStages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		ShaderStages[0].module = RayGenShaderModule;
		ShaderStages[0].pName = "main";
		ShaderStages[0].pSpecializationInfo = &SpecializationInfo;

		ShaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ShaderStages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
		ShaderStages[1].module = RayMissShaderModule;
		ShaderStages[1].pName = "main";
		ShaderStages[1].pSpecializationInfo = NULL;

		ShaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ShaderStages[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		ShaderStages[2].module = ClosestHitShaderModule;
		ShaderStages[2].pName = "main";
		ShaderStages[2].pSpecializationInfo = NULL;

		VkRayTracingShaderGroupCreateInfoKHR ShaderGroups[3] = { 0 };
		// Ray generation group
		ShaderGroups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		ShaderGroups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		ShaderGroups[0].generalShader = 0;
		ShaderGroups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

		// Miss group
		ShaderGroups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		ShaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		ShaderGroups[1].generalShader = 1;
		ShaderGroups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

		// Closest hit group
		ShaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		ShaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		ShaderGroups[2].generalShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups[2].closestHitShader = 2;
		ShaderGroups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

		{
			VkRayTracingPipelineCreateInfoKHR RayTracingPipelineCI = { 0 };
			RayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
			RayTracingPipelineCI.stageCount = ARRAYSIZE(ShaderStages);
			RayTracingPipelineCI.pStages = ShaderStages;
			RayTracingPipelineCI.groupCount = ARRAYSIZE(ShaderGroups);
			RayTracingPipelineCI.pGroups = ShaderGroups;
			RayTracingPipelineCI.maxPipelineRayRecursionDepth = min(4, RayTracingPipelineProperties.maxRayRecursionDepth);
			RayTracingPipelineCI.layout = VulkanObjects.PipelineLayout;
			THROW_ON_FAIL_VK(vkCreateRayTracingPipelinesKHR(VulkanObjects.Device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &RayTracingPipelineCI, NULL, &VulkanObjects.Pipeline));
		}

		vkDestroyShaderModule(VulkanObjects.Device, RayGenShaderModule, NULL);
		vkDestroyShaderModule(VulkanObjects.Device, RayMissShaderModule, NULL);
		vkDestroyShaderModule(VulkanObjects.Device, ClosestHitShaderModule, NULL);
	}

	struct ShaderBindingTables
	{
		VkBuffer Buffer;
		VkDeviceMemory Memory;
		VkDescriptorBufferInfo Descriptor;
		void* Mapped;
	};

	struct ShaderBindingTables ShaderBindingTables[SHADER_BINDING_COUNT] = { 0 };
	/*
		Create the Shader Binding Tables that binds the programs and top-level acceleration structure

		SBT Layout used in this sample:

			/-----------\
			| raygen    |
			|-----------|
			| miss      |
			|-----------|
			| hit       |
			\-----------/

	*/
	for (enum SHADER_BINDINGS i = 0; i < SHADER_BINDING_COUNT; i++)
	{
		{
			VkBufferCreateInfo BufferCreateInfo = { 0 };
			BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			BufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			BufferCreateInfo.size = RayTracingPipelineProperties.shaderGroupHandleSize;
			THROW_ON_FAIL_VK(vkCreateBuffer(VulkanObjects.Device, &BufferCreateInfo, NULL, &ShaderBindingTables[i].Buffer));
		}

		{
			VkMemoryRequirements MemReqs;
			vkGetBufferMemoryRequirements(VulkanObjects.Device, ShaderBindingTables[i].Buffer, &MemReqs);

			VkMemoryAllocateFlagsInfoKHR AllocFlagsInfo = { 0 };
			AllocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
			AllocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
				
			VkMemoryAllocateInfo MemAlloc = { 0 };
			MemAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemAlloc.pNext = &AllocFlagsInfo;
			MemAlloc.allocationSize = MemReqs.size;
			MemAlloc.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &MemAlloc, NULL, &ShaderBindingTables[i].Memory));
		}

		ShaderBindingTables[i].Descriptor.offset = 0;
		ShaderBindingTables[i].Descriptor.buffer = ShaderBindingTables[i].Buffer;
		ShaderBindingTables[i].Descriptor.range = VK_WHOLE_SIZE;

		THROW_ON_FAIL_VK(vkBindBufferMemory(VulkanObjects.Device, ShaderBindingTables[i].Buffer, ShaderBindingTables[i].Memory, 0));

		{
			VkBufferDeviceAddressInfoKHR BufferDeviceAI = { 0 };
			BufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			BufferDeviceAI.buffer = ShaderBindingTables[i].Buffer;

			const uint32_t HandleSizeAligned = AlignedSize(RayTracingPipelineProperties.shaderGroupHandleSize, RayTracingPipelineProperties.shaderGroupHandleAlignment);

			VulkanObjects.ShaderBindingTables[i].deviceAddress = vkGetBufferDeviceAddressKHR(VulkanObjects.Device, &BufferDeviceAI);
			VulkanObjects.ShaderBindingTables[i].stride = HandleSizeAligned;
			VulkanObjects.ShaderBindingTables[i].size = HandleSizeAligned;
		}

		vkMapMemory(VulkanObjects.Device, ShaderBindingTables[i].Memory, 0, VK_WHOLE_SIZE, 0, &ShaderBindingTables[i].Mapped);

		const uint32_t HandleSize = RayTracingPipelineProperties.shaderGroupHandleSize;
		const uint32_t HandleSizeAligned = AlignedSize(RayTracingPipelineProperties.shaderGroupHandleSize, RayTracingPipelineProperties.shaderGroupHandleAlignment);
		const uint32_t GroupCount = 3 /*ShaderGroups*/;
		const uint32_t SbtSize = GroupCount * HandleSizeAligned;

		char* ShaderHandleStorage = malloc(SbtSize);
		THROW_ON_FAIL_VK(vkGetRayTracingShaderGroupHandlesKHR(VulkanObjects.Device, VulkanObjects.Pipeline, 0, GroupCount, SbtSize, ShaderHandleStorage));
		memcpy(ShaderBindingTables[i].Mapped, ShaderHandleStorage + HandleSizeAligned * i, HandleSize);
		free(ShaderHandleStorage);
	}

	VkDescriptorPool DescriptorPool;

	{
		VkDescriptorPoolSize PoolSizes[4] = { 0 };
		PoolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		PoolSizes[0].descriptorCount = MAX_CONCURRENT_FRAMES;

		PoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		PoolSizes[1].descriptorCount = MAX_CONCURRENT_FRAMES;

		PoolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		PoolSizes[2].descriptorCount = MAX_CONCURRENT_FRAMES;

		PoolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		PoolSizes[3].descriptorCount = MAX_CONCURRENT_FRAMES * 2;

		VkDescriptorPoolCreateInfo DescriptorPoolCreateInfo = { 0 };
		DescriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		DescriptorPoolCreateInfo.poolSizeCount = ARRAYSIZE(PoolSizes);
		DescriptorPoolCreateInfo.pPoolSizes = PoolSizes;
		DescriptorPoolCreateInfo.maxSets = MAX_CONCURRENT_FRAMES;
		THROW_ON_FAIL_VK(vkCreateDescriptorPool(VulkanObjects.Device, &DescriptorPoolCreateInfo, NULL, &DescriptorPool));
	}

	for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++)
	{
		{
			VkDescriptorSetAllocateInfo AllocInfo = { 0 };
			AllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			AllocInfo.descriptorPool = DescriptorPool;
			AllocInfo.pSetLayouts = &DescriptorSetLayout;
			AllocInfo.descriptorSetCount = 1;
			THROW_ON_FAIL_VK(vkAllocateDescriptorSets(VulkanObjects.Device, &AllocInfo, &VulkanObjects.DescriptorSets[i]));
		}

		VkWriteDescriptorSetAccelerationStructureKHR DescriptorAccelerationStructureInfo = { 0 };
		DescriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		DescriptorAccelerationStructureInfo.accelerationStructureCount = 1;
		DescriptorAccelerationStructureInfo.pAccelerationStructures = &TopLevelAS.Handle;

		VkDescriptorBufferInfo VertexBufferDescriptor = { 0 };
		VertexBufferDescriptor.buffer = SceneGeometryStruct.Vertices.Buffer;
		VertexBufferDescriptor.offset = 0;
		VertexBufferDescriptor.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo IndexBufferDescriptor = { 0 };
		IndexBufferDescriptor.buffer = SceneGeometryStruct.Indices.Buffer;
		IndexBufferDescriptor.offset = 0;
		IndexBufferDescriptor.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet WriteDescriptorSets[4] = { 0 };

		// Binding 0: Top level acceleration structure
		// The specialized acceleration structure descriptor has to be chained
		WriteDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		WriteDescriptorSets[0].pNext = &DescriptorAccelerationStructureInfo;
		WriteDescriptorSets[0].dstSet = VulkanObjects.DescriptorSets[i];
		WriteDescriptorSets[0].dstBinding = 0;
		WriteDescriptorSets[0].descriptorCount = 1;
		WriteDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		// Binding 1: Ray tracing result image (reserved to be filled in later)


		// Binding 2: Uniform data
		WriteDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		WriteDescriptorSets[1].dstSet = VulkanObjects.DescriptorSets[i];
		WriteDescriptorSets[1].dstBinding = 2;
		WriteDescriptorSets[1].descriptorCount = 1;
		WriteDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		WriteDescriptorSets[1].pBufferInfo = &UniformBuffers[i].UnifromBufferDescriptor;

		// Binding 3: Scene vertex buffer
		WriteDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		WriteDescriptorSets[2].dstSet = VulkanObjects.DescriptorSets[i];
		WriteDescriptorSets[2].dstBinding = 3;
		WriteDescriptorSets[2].descriptorCount = 1;
		WriteDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		WriteDescriptorSets[2].pBufferInfo = &VertexBufferDescriptor;

		WriteDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		WriteDescriptorSets[3].dstSet = VulkanObjects.DescriptorSets[i];
		WriteDescriptorSets[3].dstBinding = 4;
		WriteDescriptorSets[3].descriptorCount = 1;
		WriteDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		WriteDescriptorSets[3].pBufferInfo = &IndexBufferDescriptor;
		vkUpdateDescriptorSets(VulkanObjects.Device, ARRAYSIZE(WriteDescriptorSets), WriteDescriptorSets, 0, VK_NULL_HANDLE);
	}

	THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);

	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_INIT,
		.wParam = (WPARAM)&VulkanObjects,
		.lParam = 0
	});

	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_SIZE,
		.wParam = SIZE_RESTORED,
		.lParam = MAKELONG(WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top)
	});

	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
	}

	vkDeviceWaitIdle(VulkanObjects.Device);

	vkDestroyBuffer(VulkanObjects.Device, SceneGeometryStruct.Vertices.Buffer, NULL);
	vkFreeMemory(VulkanObjects.Device, SceneGeometryStruct.Vertices.Memory, NULL);
	vkDestroyBuffer(VulkanObjects.Device, SceneGeometryStruct.Indices.Buffer, NULL);
	vkFreeMemory(VulkanObjects.Device, SceneGeometryStruct.Indices.Memory, NULL);

	vkDestroyPipeline(VulkanObjects.Device, VulkanObjects.Pipeline, NULL);
	vkDestroyPipelineLayout(VulkanObjects.Device, VulkanObjects.PipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(VulkanObjects.Device, DescriptorSetLayout, NULL);

	vkDestroyImageView(VulkanObjects.Device, VulkanObjects.StorageImage.View, NULL);
	vkDestroyImage(VulkanObjects.Device, VulkanObjects.StorageImage.Image, NULL);
	vkFreeMemory(VulkanObjects.Device, VulkanObjects.StorageImage.Memory, NULL);
	
	vkFreeMemory(VulkanObjects.Device, BottomLevelAS.Memory, NULL);
	vkDestroyBuffer(VulkanObjects.Device, BottomLevelAS.Buffer, NULL);
	vkDestroyAccelerationStructureKHR(VulkanObjects.Device, BottomLevelAS.Handle, NULL);

	vkFreeMemory(VulkanObjects.Device, TopLevelAS.Memory, NULL);
	vkDestroyBuffer(VulkanObjects.Device, TopLevelAS.Buffer, NULL);
	vkDestroyAccelerationStructureKHR(VulkanObjects.Device, TopLevelAS.Handle, NULL);

	for (enum SHADER_BINDINGS i = 0; i < SHADER_BINDING_COUNT; i++)
	{
		vkDestroyBuffer(VulkanObjects.Device, ShaderBindingTables[i].Buffer, NULL);
		vkFreeMemory(VulkanObjects.Device, ShaderBindingTables[i].Memory, NULL);
	}
	
	for (int i = 0; i < ARRAYSIZE(UniformBuffers); i++)
	{
		vkDestroyBuffer(VulkanObjects.Device, UniformBuffers[i].UnifromBuffer, NULL);
		vkFreeMemory(VulkanObjects.Device, UniformBuffers[i].UnifromBufferMemory, NULL);
	}

	for (int i = 0; i < VulkanObjects.SwapChainImageCount; i++)
	{
		vkDestroyImageView(VulkanObjects.Device, VulkanObjects.swapChain.ImageViews[i], NULL);
	}

	vkDestroySwapchainKHR(VulkanObjects.Device, VulkanObjects.swapChain.SwapChain, NULL);

	vkDestroySurfaceKHR(VulkanInstance, VulkanObjects.swapChain.Surface, NULL);

	vkDestroyDescriptorPool(VulkanObjects.Device, DescriptorPool, NULL);

	vkFreeCommandBuffers(VulkanObjects.Device, CommandPool, ARRAYSIZE(VulkanObjects.DrawCmdBuffers), VulkanObjects.DrawCmdBuffers);

	vkDestroyRenderPass(VulkanObjects.Device, VulkanObjects.RenderPass, NULL);

	for (int i = 0; i < VulkanObjects.SwapChainImageCount; i++)
	{
		vkDestroyFramebuffer(VulkanObjects.Device, VulkanObjects.FrameBuffers[i], NULL);
	}

	vkDestroyImageView(VulkanObjects.Device, VulkanObjects.DepthStencil.View, NULL);
	vkDestroyImage(VulkanObjects.Device, VulkanObjects.DepthStencil.Image, NULL);
	vkFreeMemory(VulkanObjects.Device, VulkanObjects.DepthStencil.Memory, NULL);
	vkDestroyCommandPool(VulkanObjects.Device, CommandPool, NULL);

	for (int i = 0; i < ARRAYSIZE(VulkanObjects.WaitFences); i++)
	{
		vkDestroyFence(VulkanObjects.Device, VulkanObjects.WaitFences[i], NULL);
	}

	for (int i = 0; i < ARRAYSIZE(VulkanObjects.PresentCompleteSemaphores); i++)
	{
		vkDestroySemaphore(VulkanObjects.Device, VulkanObjects.PresentCompleteSemaphores[i], NULL);
	}

	for (int i = 0; i < VulkanObjects.SwapChainImageCount; i++)
	{
		vkDestroySemaphore(VulkanObjects.Device, VulkanObjects.RenderCompleteSemaphores[i], NULL);
	}

	vkDestroyCommandPool(VulkanObjects.Device, VulkanObjects.CommandPool, NULL);
	vkDestroyDevice(VulkanObjects.Device, NULL);

#ifdef _DEBUG
	{
		PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(VulkanInstance, "vkDestroyDebugUtilsMessengerEXT");
		vkDestroyDebugUtilsMessengerEXT(VulkanInstance, DebugUtilsMessenger, NULL);
	}
#endif

	vkDestroyInstance(VulkanInstance, NULL);

	THROW_ON_FALSE(UnregisterClassW(ApplicationName, Instance));

	THROW_ON_FALSE(DestroyCursor(Cursor));
	THROW_ON_FALSE(DestroyIcon(Icon));

	return EXIT_SUCCESS;
}

LRESULT CALLBACK PreInitProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK IdleProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		Sleep(25);
		break;
	case WM_SIZE:
		if (wParam == SIZE_RESTORED)
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK WndProc(HWND Window, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static const float CAMERA_ROTATION_SPEED = 0.25f;
	static const float CAMERA_MOVEMENT_SPEED = 1.0f;
	static const float TIMER_SPEED = 0.0125f;

	static bool Paused = false;
	static bool bVsync = false;

	static bool bSwapchainInitialized = false;

	static int CurrentBuffer = 0;

	static uint32_t WindowWidth;
	static uint32_t WindowHeight;

	static struct VulkanObjects* restrict VulkanObjects = NULL;

	static struct
	{
		bool Left;
		bool Right;
		bool Up;
		bool Down;
	} Keys = { 0 };

	static struct
	{
		struct
		{
			bool Left;
			bool Right;
			bool Middle;
		} Buttons;
		vec2 Position;
	} MouseState = { 0 };

	static struct
	{
		LARGE_INTEGER ProcessorFrequency;
		LARGE_INTEGER TickCount;
	} Timer = { 0 };
	
	switch (uMsg)
	{
	case WM_INIT:
		QueryPerformanceFrequency(&Timer.ProcessorFrequency);
		VulkanObjects = ((struct VulkanObjects*)wParam);

		VulkanObjects->Camera.CameraType = CAMERA_TYPE_FIRST_PERSON;
		glm_vec3_copy((vec3) { 0.0f, 0.5f, -2.0f }, VulkanObjects->Camera.Position);
		glm_vec3_copy((vec3) { 0.0f, 0.0f, 0.0f }, VulkanObjects->Camera.Rotation);
		break;
	case WM_CLOSE:
		DestroyWindow(Window);
		PostQuitMessage(0);
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case 'P':
			Paused = !Paused;
			break;
		case VK_F2:
			if (VulkanObjects->Camera.CameraType == CAMERA_TYPE_LOOKAT)
			{
				VulkanObjects->Camera.CameraType = CAMERA_TYPE_FIRST_PERSON;
			}
			else
			{
				VulkanObjects->Camera.CameraType = CAMERA_TYPE_LOOKAT;
			}
			break;
		case VK_ESCAPE:
			PostQuitMessage(0);
			break;
		}

		if (VulkanObjects->Camera.CameraType == CAMERA_TYPE_FIRST_PERSON)
		{
			switch (wParam)
			{
			case 'W':
				Keys.Up = true;
				break;
			case 'S':
				Keys.Down = true;
				break;
			case 'A':
				Keys.Left = true;
				break;
			case 'D':
				Keys.Right = true;
				break;
			}
		}
		break;
	case WM_KEYUP:
		if (VulkanObjects->Camera.CameraType == CAMERA_TYPE_FIRST_PERSON)
		{
			switch (wParam)
			{
			case 'W':
				Keys.Up = false;
				break;
			case 'S':
				Keys.Down = false;
				break;
			case 'A':
				Keys.Left = false;
				break;
			case 'D':
				Keys.Right = false;
				break;
			}
		}
		break;
	case WM_LBUTTONDOWN:
		glm_vec2_copy((vec2) { (float)LOWORD(lParam), (float)HIWORD(lParam) }, MouseState.Position);
		MouseState.Buttons.Left = true;
		break;
	case WM_RBUTTONDOWN:
		glm_vec2_copy((vec2) { (float)LOWORD(lParam), (float)HIWORD(lParam) }, MouseState.Position);
		MouseState.Buttons.Right = true;
		break;
	case WM_MBUTTONDOWN:
		glm_vec2_copy((vec2) { (float)LOWORD(lParam), (float)HIWORD(lParam) }, MouseState.Position);
		MouseState.Buttons.Middle = true;
		break;
	case WM_LBUTTONUP:
		MouseState.Buttons.Left = false;
		break;
	case WM_RBUTTONUP:
		MouseState.Buttons.Right = false;
		break;
	case WM_MBUTTONUP:
		MouseState.Buttons.Middle = false;
		break;
	case WM_MOUSEWHEEL:
	{
		short WheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		glm_vec3_add(VulkanObjects->Camera.Position, (vec3) { 0.0f, 0.0f, (float)WheelDelta * 0.005f }, VulkanObjects->Camera.Position);
		updateViewMatrix(&VulkanObjects->Camera);
		break;
	}
	case WM_MOUSEMOVE:
	{
		const int32_t x = LOWORD(lParam);
		const int32_t y = HIWORD(lParam);
		const int32_t dx = (int32_t)MouseState.Position[0] - x;
		const int32_t dy = (int32_t)MouseState.Position[1] - y;

		if (MouseState.Buttons.Left)
		{
			glm_vec3_add((vec3) { dy * CAMERA_ROTATION_SPEED, -dx * CAMERA_ROTATION_SPEED, 0.0f }, VulkanObjects->Camera.Rotation, VulkanObjects->Camera.Rotation);
			updateViewMatrix(&VulkanObjects->Camera);
		}

		if (MouseState.Buttons.Right)
		{
			glm_vec3_add(VulkanObjects->Camera.Position, (vec3) { -0.0f, 0.0f, dy * .005f }, VulkanObjects->Camera.Position);
			updateViewMatrix(&VulkanObjects->Camera);
		}

		if (MouseState.Buttons.Middle)
		{
			glm_vec3_add(VulkanObjects->Camera.Position, (vec3) { -dx * 0.005f, -dy * 0.005f, 0.0f }, VulkanObjects->Camera.Position);
			updateViewMatrix(&VulkanObjects->Camera);
		}

		glm_vec2_copy((vec2) { (float)x, (float)y }, MouseState.Position);
		break;
	}
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
		{
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)IdleProc) != 0);
			break;
		}

		if (WindowWidth == LOWORD(lParam) && WindowHeight == HIWORD(lParam))
			break;

		WindowWidth = LOWORD(lParam);
		WindowHeight = HIWORD(lParam);

		glm_perspective_rh_no(glm_rad(60.0f),
			(float)WindowWidth / (float)WindowHeight,
			0.1f,
			512.0f,
			VulkanObjects->Camera.Matrices.Perspective);

		updateViewMatrix(&VulkanObjects->Camera);

		vkDeviceWaitIdle(VulkanObjects->Device);

		if (bSwapchainInitialized)
		{
			vkDestroyImageView(VulkanObjects->Device, VulkanObjects->DepthStencil.View, NULL);
			vkDestroyImage(VulkanObjects->Device, VulkanObjects->DepthStencil.Image, NULL);
			vkFreeMemory(VulkanObjects->Device, VulkanObjects->DepthStencil.Memory, NULL);

			for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
			{
				vkDestroyFramebuffer(VulkanObjects->Device, VulkanObjects->FrameBuffers[i], NULL);
			}

			for (int i = 0; i < ARRAYSIZE(VulkanObjects->PresentCompleteSemaphores); i++)
			{
				vkDestroySemaphore(VulkanObjects->Device, VulkanObjects->PresentCompleteSemaphores[i], NULL);
			}

			for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
			{
				vkDestroySemaphore(VulkanObjects->Device, VulkanObjects->RenderCompleteSemaphores[i], NULL);
			}

			for (int i = 0; i < ARRAYSIZE(VulkanObjects->WaitFences); i++)
			{
				vkDestroyFence(VulkanObjects->Device, VulkanObjects->WaitFences[i], NULL);
			}
		}
			
		{
			VkSwapchainKHR OldSwapchain = VulkanObjects->swapChain.SwapChain;

			VkSurfaceCapabilitiesKHR SurfaceCaps;
			THROW_ON_FAIL_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VulkanObjects->PhysicalDevice, VulkanObjects->swapChain.Surface, &SurfaceCaps));

			VkExtent2D SwapchainExtent = { 0 };
			if (SurfaceCaps.currentExtent.width == UINT32_MAX)
			{
				SwapchainExtent.width = WindowWidth;
				SwapchainExtent.height = WindowHeight;
			}
			else
			{
				SwapchainExtent = SurfaceCaps.currentExtent;
				WindowWidth = SurfaceCaps.currentExtent.width;
				WindowHeight = SurfaceCaps.currentExtent.height;
			}

			uint32_t PresentModeCount;
			VkPresentModeKHR PresentModes[MAX_PRESENT_MODES];
			THROW_ON_FAIL_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(VulkanObjects->PhysicalDevice, VulkanObjects->swapChain.Surface, &PresentModeCount, NULL));
			THROW_ON_FAIL_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(VulkanObjects->PhysicalDevice, VulkanObjects->swapChain.Surface, &PresentModeCount, PresentModes));

			VkPresentModeKHR SwapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

			if (!bVsync)
			{
				for (int i = 0; i < PresentModeCount; i++)
				{
					if (PresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
					{
						SwapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
						break;
					}
					if (PresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
					{
						SwapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
					}
				}
			}

			uint32_t DesiredNumberOfSwapchainImages = SurfaceCaps.minImageCount + 1;
			if ((SurfaceCaps.maxImageCount > 0) && (DesiredNumberOfSwapchainImages > SurfaceCaps.maxImageCount))
			{
				DesiredNumberOfSwapchainImages = SurfaceCaps.maxImageCount;
			}

			VkSurfaceTransformFlagsKHR PreTransform;
			if (SurfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
			{
				PreTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
			}
			else
			{
				PreTransform = SurfaceCaps.currentTransform;
			}

			VkCompositeAlphaFlagBitsKHR CompositeAlphaFlags[] = {
				VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
				VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
				VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
				VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
			};

			VkCompositeAlphaFlagBitsKHR CompositeAlpha = CompositeAlphaFlags[0];

			for (int i = 0; i < ARRAYSIZE(CompositeAlphaFlags); i++)
			{
				if (SurfaceCaps.supportedCompositeAlpha & CompositeAlphaFlags[i])
				{
					CompositeAlpha = CompositeAlphaFlags[i];
					break;
				}
			}

			{
				VkSwapchainCreateInfoKHR SwapchainCI = { 0 };
				SwapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
				SwapchainCI.surface = VulkanObjects->swapChain.Surface;
				SwapchainCI.minImageCount = DesiredNumberOfSwapchainImages;
				SwapchainCI.imageFormat = VulkanObjects->swapChain.ColorFormat;
				SwapchainCI.imageColorSpace = VulkanObjects->swapChain.ColorSpace;
				SwapchainCI.imageExtent.width = SwapchainExtent.width;
				SwapchainCI.imageExtent.height = SwapchainExtent.height;
				SwapchainCI.imageArrayLayers = 1;
				SwapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
				SwapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				SwapchainCI.queueFamilyIndexCount = 0;
				SwapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR)PreTransform;
				SwapchainCI.compositeAlpha = CompositeAlpha;
				SwapchainCI.presentMode = SwapchainPresentMode;
				SwapchainCI.clipped = VK_TRUE;
				SwapchainCI.oldSwapchain = OldSwapchain;

				if (SurfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
				{
					SwapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
				}

				if (SurfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
				{
					SwapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				}

				THROW_ON_FAIL_VK(vkCreateSwapchainKHR(VulkanObjects->Device, &SwapchainCI, NULL, &VulkanObjects->swapChain.SwapChain));
			}

			if (OldSwapchain != VK_NULL_HANDLE)
			{
				for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
				{
					vkDestroyImageView(VulkanObjects->Device, VulkanObjects->swapChain.ImageViews[i], NULL);
				}

				vkDestroySwapchainKHR(VulkanObjects->Device, OldSwapchain, NULL);
			}
		}
			
		THROW_ON_FAIL_VK(vkGetSwapchainImagesKHR(VulkanObjects->Device, VulkanObjects->swapChain.SwapChain, &VulkanObjects->SwapChainImageCount, NULL));
		THROW_ON_FAIL_VK(vkGetSwapchainImagesKHR(VulkanObjects->Device, VulkanObjects->swapChain.SwapChain, &VulkanObjects->SwapChainImageCount, VulkanObjects->swapChain.Images));
			
		for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
		{
			VkImageViewCreateInfo ColorAttachmentView = { 0 };
			ColorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ColorAttachmentView.image = VulkanObjects->swapChain.Images[i];
			ColorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ColorAttachmentView.format = VulkanObjects->swapChain.ColorFormat;
			ColorAttachmentView.components.r = VK_COMPONENT_SWIZZLE_R;
			ColorAttachmentView.components.g = VK_COMPONENT_SWIZZLE_G;
			ColorAttachmentView.components.b = VK_COMPONENT_SWIZZLE_B;
			ColorAttachmentView.components.a = VK_COMPONENT_SWIZZLE_A;
			ColorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ColorAttachmentView.subresourceRange.baseMipLevel = 0;
			ColorAttachmentView.subresourceRange.levelCount = 1;
			ColorAttachmentView.subresourceRange.baseArrayLayer = 0;
			ColorAttachmentView.subresourceRange.layerCount = 1;
			THROW_ON_FAIL_VK(vkCreateImageView(VulkanObjects->Device, &ColorAttachmentView, NULL, &VulkanObjects->swapChain.ImageViews[i]));
		}

		{
			VkImageCreateInfo ImageCI = { 0 };
			ImageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ImageCI.imageType = VK_IMAGE_TYPE_2D;
			ImageCI.format = VulkanObjects->DepthFormat;
			ImageCI.extent.width = WindowWidth;
			ImageCI.extent.height = WindowHeight;
			ImageCI.extent.depth = 1;
			ImageCI.mipLevels = 1;
			ImageCI.arrayLayers = 1;
			ImageCI.samples = VK_SAMPLE_COUNT_1_BIT;
			ImageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
			ImageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			THROW_ON_FAIL_VK(vkCreateImage(VulkanObjects->Device, &ImageCI, NULL, &VulkanObjects->DepthStencil.Image));
		}

		{
			VkMemoryRequirements MemReqs;
			vkGetImageMemoryRequirements(VulkanObjects->Device, VulkanObjects->DepthStencil.Image, &MemReqs);

			VkMemoryAllocateInfo MemAllloc = { 0 };
			MemAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemAllloc.allocationSize = MemReqs.size;
			MemAllloc.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects->Device, &MemAllloc, NULL, &VulkanObjects->DepthStencil.Memory));
		}

		THROW_ON_FAIL_VK(vkBindImageMemory(VulkanObjects->Device, VulkanObjects->DepthStencil.Image, VulkanObjects->DepthStencil.Memory, 0));

		{
			VkImageViewCreateInfo ImageViewCI = { 0 };
			ImageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ImageViewCI.image = VulkanObjects->DepthStencil.Image;
			ImageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ImageViewCI.format = VulkanObjects->DepthFormat;
			ImageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			ImageViewCI.subresourceRange.baseMipLevel = 0;
			ImageViewCI.subresourceRange.levelCount = 1;
			ImageViewCI.subresourceRange.baseArrayLayer = 0;
			ImageViewCI.subresourceRange.layerCount = 1;

			if (VulkanObjects->DepthFormat >= VK_FORMAT_D16_UNORM_S8_UINT)
			{
				ImageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}

			THROW_ON_FAIL_VK(vkCreateImageView(VulkanObjects->Device, &ImageViewCI, NULL, &VulkanObjects->DepthStencil.View));
		}

		for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
		{
			VkImageView Attachments[2] = {
				VulkanObjects->swapChain.ImageViews[i],
				VulkanObjects->DepthStencil.View
			};

			VkFramebufferCreateInfo FrameBufferCreateInfo = { 0 };
			FrameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			FrameBufferCreateInfo.renderPass = VulkanObjects->RenderPass;
			FrameBufferCreateInfo.attachmentCount = 2;
			FrameBufferCreateInfo.pAttachments = Attachments;
			FrameBufferCreateInfo.width = WindowWidth;
			FrameBufferCreateInfo.height = WindowHeight;
			FrameBufferCreateInfo.layers = 1;
			THROW_ON_FAIL_VK(vkCreateFramebuffer(VulkanObjects->Device, &FrameBufferCreateInfo, NULL, &VulkanObjects->FrameBuffers[i]));
		}

		for (int i = 0; i < ARRAYSIZE(VulkanObjects->WaitFences); i++)
		{
			VkFenceCreateInfo FenceCreateInfo = { 0 };
			FenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			FenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			THROW_ON_FAIL_VK(vkCreateFence(VulkanObjects->Device, &FenceCreateInfo, NULL, &VulkanObjects->WaitFences[i]));
		}

		for (int i = 0; i < ARRAYSIZE(VulkanObjects->PresentCompleteSemaphores); i++)
		{
			VkSemaphoreCreateInfo SemaphoreCI = { 0 };
			SemaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			THROW_ON_FAIL_VK(vkCreateSemaphore(VulkanObjects->Device, &SemaphoreCI, NULL, &VulkanObjects->PresentCompleteSemaphores[i]));
		}

		for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
		{
			VkSemaphoreCreateInfo SemaphoreCI = { 0 };
			SemaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			THROW_ON_FAIL_VK(vkCreateSemaphore(VulkanObjects->Device, &SemaphoreCI, NULL, &VulkanObjects->RenderCompleteSemaphores[i]));
		}

		bSwapchainInitialized = true;
		vkDeviceWaitIdle(VulkanObjects->Device);

		glm_perspective_rh_no(
			glm_rad(60.0f),
			(float)WindowWidth / (float)WindowHeight,
			0.1f,
			512.0f,
			VulkanObjects->Camera.Matrices.Perspective);

		if (VulkanObjects->StorageImage.Image != VK_NULL_HANDLE)
		{
			vkDestroyImageView(VulkanObjects->Device, VulkanObjects->StorageImage.View, NULL);
			vkDestroyImage(VulkanObjects->Device, VulkanObjects->StorageImage.Image, NULL);
			vkFreeMemory(VulkanObjects->Device, VulkanObjects->StorageImage.Memory, NULL);
			memset(&VulkanObjects->StorageImage, 0, sizeof(VulkanObjects->StorageImage));
		}

		{
			VkImageCreateInfo Image = { 0 };
			Image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			Image.imageType = VK_IMAGE_TYPE_2D;
			Image.format = VulkanObjects->swapChain.ColorFormat;
			Image.extent.width = WindowWidth;
			Image.extent.height = WindowHeight;
			Image.extent.depth = 1;
			Image.mipLevels = 1;
			Image.arrayLayers = 1;
			Image.samples = VK_SAMPLE_COUNT_1_BIT;
			Image.tiling = VK_IMAGE_TILING_OPTIMAL;
			Image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
			Image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			THROW_ON_FAIL_VK(vkCreateImage(VulkanObjects->Device, &Image, NULL, &VulkanObjects->StorageImage.Image));
		}

		{
			VkMemoryRequirements MemReqs;
			vkGetImageMemoryRequirements(VulkanObjects->Device, VulkanObjects->StorageImage.Image, &MemReqs);

			VkMemoryAllocateInfo MemoryAllocateInfo = { 0 };
			MemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemoryAllocateInfo.allocationSize = MemReqs.size;
			MemoryAllocateInfo.memoryTypeIndex = GetMemoryType(&VulkanObjects, MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects->Device, &MemoryAllocateInfo, NULL, &VulkanObjects->StorageImage.Memory));
		}

		THROW_ON_FAIL_VK(vkBindImageMemory(VulkanObjects->Device, VulkanObjects->StorageImage.Image, VulkanObjects->StorageImage.Memory, 0));

		{
			VkImageViewCreateInfo ColorImageView = { 0 };
			ColorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ColorImageView.image = VulkanObjects->StorageImage.Image;
			ColorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ColorImageView.format = VulkanObjects->swapChain.ColorFormat;
			ColorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ColorImageView.subresourceRange.baseMipLevel = 0;
			ColorImageView.subresourceRange.levelCount = 1;
			ColorImageView.subresourceRange.baseArrayLayer = 0;
			ColorImageView.subresourceRange.layerCount = 1;
			THROW_ON_FAIL_VK(vkCreateImageView(VulkanObjects->Device, &ColorImageView, NULL, &VulkanObjects->StorageImage.View));
		}

		{
			VkCommandBuffer CmdBuffer;

			{
				VkCommandBufferAllocateInfo CmdBufAllocateInfo = { 0 };
				CmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				CmdBufAllocateInfo.commandPool = VulkanObjects->CommandPool;
				CmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				CmdBufAllocateInfo.commandBufferCount = 1;
				THROW_ON_FAIL_VK(vkAllocateCommandBuffers(VulkanObjects->Device, &CmdBufAllocateInfo, &CmdBuffer));
			}

			{
				VkCommandBufferBeginInfo CmdBufInfo = { 0 };
				CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				THROW_ON_FAIL_VK(vkBeginCommandBuffer(CmdBuffer, &CmdBufInfo));
			}

			{
				VkImageMemoryBarrier ImageMemoryBarrier = { 0 };
				ImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				ImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				ImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				ImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				ImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				ImageMemoryBarrier.image = VulkanObjects->StorageImage.Image;
				ImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				ImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
				ImageMemoryBarrier.subresourceRange.levelCount = 1;
				ImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
				ImageMemoryBarrier.subresourceRange.layerCount = 1;

				vkCmdPipelineBarrier(
					CmdBuffer,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
					0,
					0,
					NULL,
					0,
					NULL,
					1,
					&ImageMemoryBarrier);
			}

			THROW_ON_FAIL_VK(vkEndCommandBuffer(CmdBuffer));

			{
				VkFence Fence;

				{
					VkFenceCreateInfo FenceInfo = { 0 };
					FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
					FenceInfo.flags = VK_FLAGS_NONE;
					THROW_ON_FAIL_VK(vkCreateFence(VulkanObjects->Device, &FenceInfo, NULL, &Fence));
				}

				{
					VkSubmitInfo SubmitInfo = { 0 };
					SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
					SubmitInfo.commandBufferCount = 1;
					SubmitInfo.pCommandBuffers = &CmdBuffer;
					THROW_ON_FAIL_VK(vkQueueSubmit(VulkanObjects->Queue, 1, &SubmitInfo, Fence));
				}

				THROW_ON_FAIL_VK(vkWaitForFences(VulkanObjects->Device, 1, &Fence, VK_TRUE, UINT64_MAX));
				vkDestroyFence(VulkanObjects->Device, Fence, NULL);

				vkFreeCommandBuffers(VulkanObjects->Device, VulkanObjects->CommandPool, 1, &CmdBuffer);
			}
		}

		for (int i = 0; i < ARRAYSIZE(VulkanObjects->DescriptorSets); i++)
		{
			VkDescriptorImageInfo StorageImageDescriptor = { 0 };
			StorageImageDescriptor.sampler = VK_NULL_HANDLE;
			StorageImageDescriptor.imageView = VulkanObjects->StorageImage.View;
			StorageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			VkWriteDescriptorSet ResultImageWrite = { 0 };
			ResultImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			ResultImageWrite.dstSet = VulkanObjects->DescriptorSets[i];
			ResultImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			ResultImageWrite.dstBinding = 1;
			ResultImageWrite.pImageInfo = &StorageImageDescriptor;
			ResultImageWrite.descriptorCount = 1;
			vkUpdateDescriptorSets(VulkanObjects->Device, 1, &ResultImageWrite, 0, VK_NULL_HANDLE);
		}
		break;
	case WM_PAINT:
	{
		LARGE_INTEGER TickCountNow;
		QueryPerformanceCounter(&TickCountNow);
		ULONGLONG TickCountDelta = TickCountNow.QuadPart - Timer.TickCount.QuadPart;
		Timer.TickCount.QuadPart = TickCountNow.QuadPart;

		float MovementFactor = (TickCountDelta / ((float)Timer.ProcessorFrequency.QuadPart)) * 75.f;

		THROW_ON_FAIL_VK(vkWaitForFences(VulkanObjects->Device, 1, &VulkanObjects->WaitFences[CurrentBuffer], VK_TRUE, UINT64_MAX));
		THROW_ON_FAIL_VK(vkResetFences(VulkanObjects->Device, 1, &VulkanObjects->WaitFences[CurrentBuffer]));

		uint32_t CurrentImageIndex;
		THROW_ON_FAIL_VK(vkAcquireNextImageKHR(
			VulkanObjects->Device,
			VulkanObjects->swapChain.SwapChain,
			UINT64_MAX,
			VulkanObjects->PresentCompleteSemaphores[CurrentBuffer],
			VK_NULL_HANDLE,
			&CurrentImageIndex));

		{
			struct UniformData UniformData = { 0 };
			glm_mat4_inv(VulkanObjects->Camera.Matrices.Perspective, UniformData.ProjInverse);
			glm_mat4_inv(VulkanObjects->Camera.Matrices.View, UniformData.ViewInverse);
			glm_vec4_copy((vec4) { cos(glm_rad(VulkanObjects->Timer * 360.0f)) * 40.0f, -20.0f + sin(glm_rad(VulkanObjects->Timer * 360.0f)) * 20.0f, 25.0f + sin(glm_rad(VulkanObjects->Timer * 360.0f)) * 5.0f, 0.0f }, UniformData.LightPos);
				
			UniformData.VertexSize = sizeof(struct Vertex);
			memcpy(VulkanObjects->UniformBufferPointers[CurrentBuffer], &UniformData, sizeof(UniformData));
		}

		{
			VkCommandBufferBeginInfo CmdBufInfo = { 0 };
			CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			THROW_ON_FAIL_VK(vkBeginCommandBuffer(VulkanObjects->DrawCmdBuffers[CurrentBuffer], &CmdBufInfo));
		}

		vkCmdBindPipeline(VulkanObjects->DrawCmdBuffers[CurrentBuffer], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, VulkanObjects->Pipeline);
		vkCmdBindDescriptorSets(VulkanObjects->DrawCmdBuffers[CurrentBuffer], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, VulkanObjects->PipelineLayout, 0, 1, &VulkanObjects->DescriptorSets[CurrentBuffer], 0, 0);

		{
			VkStridedDeviceAddressRegionKHR EmptySbtEntry = { 0 };
			VulkanObjects->vkCmdTraceRaysKHR(
				VulkanObjects->DrawCmdBuffers[CurrentBuffer],
				&VulkanObjects->ShaderBindingTables[SHADER_BINDING_RAYGEN],
				&VulkanObjects->ShaderBindingTables[SHADER_BINDING_MISS],
				&VulkanObjects->ShaderBindingTables[SHADER_BINDING_HIT],
				&EmptySbtEntry,
				WindowWidth,
				WindowHeight,
				1);
		}

		{
			VkImageMemoryBarrier ImageMemoryBarrier = { 0 };
			ImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			ImageMemoryBarrier.srcAccessMask = VK_ACCESS_NONE;
			ImageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			ImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			ImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			ImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			ImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			ImageMemoryBarrier.image = VulkanObjects->swapChain.Images[CurrentImageIndex];
			ImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
			ImageMemoryBarrier.subresourceRange.levelCount = 1;
			ImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
			ImageMemoryBarrier.subresourceRange.layerCount = 1;
			vkCmdPipelineBarrier(
				VulkanObjects->DrawCmdBuffers[CurrentBuffer],
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0,
				0,
				NULL,
				0,
				NULL,
				1,
				&ImageMemoryBarrier);
		}

		{
			VkImageMemoryBarrier ImageMemoryBarrier = { 0 };
			ImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			ImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			ImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			ImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			ImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			ImageMemoryBarrier.image = VulkanObjects->StorageImage.Image;
			ImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
			ImageMemoryBarrier.subresourceRange.levelCount = 1;
			ImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
			ImageMemoryBarrier.subresourceRange.layerCount = 1;
			ImageMemoryBarrier.srcAccessMask = VK_ACCESS_NONE;
			ImageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(
				VulkanObjects->DrawCmdBuffers[CurrentBuffer],
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0,
				0,
				NULL,
				0,
				NULL,
				1,
				&ImageMemoryBarrier);
		}

		{
			VkImageCopy CopyRegion = { 0 };
			CopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			CopyRegion.srcSubresource.mipLevel = 0;
			CopyRegion.srcSubresource.baseArrayLayer = 0;
			CopyRegion.srcSubresource.layerCount = 1;
			CopyRegion.srcOffset.x = 0;
			CopyRegion.srcOffset.y = 0;
			CopyRegion.srcOffset.z = 0;
			CopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			CopyRegion.dstSubresource.mipLevel = 0;
			CopyRegion.dstSubresource.baseArrayLayer = 0;
			CopyRegion.dstSubresource.layerCount = 1;
			CopyRegion.dstOffset.x = 0;
			CopyRegion.dstOffset.y = 0;
			CopyRegion.dstOffset.z = 0;
			CopyRegion.extent.width = WindowWidth;
			CopyRegion.extent.height = WindowHeight;
			CopyRegion.extent.depth = 1;
			vkCmdCopyImage(
				VulkanObjects->DrawCmdBuffers[CurrentBuffer],
				VulkanObjects->StorageImage.Image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VulkanObjects->swapChain.Images[CurrentImageIndex],
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&CopyRegion);
		}

		{
			VkImageMemoryBarrier ImageMemoryBarrier = { 0 };
			ImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			ImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			ImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			ImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			ImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			ImageMemoryBarrier.image = VulkanObjects->swapChain.Images[CurrentImageIndex];
			ImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
			ImageMemoryBarrier.subresourceRange.levelCount = 1;
			ImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
			ImageMemoryBarrier.subresourceRange.layerCount = 1;
			ImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			ImageMemoryBarrier.dstAccessMask = VK_ACCESS_NONE;
			vkCmdPipelineBarrier(
				VulkanObjects->DrawCmdBuffers[CurrentBuffer],
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0,
				0,
				NULL,
				0,
				NULL,
				1,
				&ImageMemoryBarrier);
		}

		{
			VkImageMemoryBarrier ImageMemoryBarrier = { 0 };
			ImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			ImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			ImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			ImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			ImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			ImageMemoryBarrier.image = VulkanObjects->StorageImage.Image;
			ImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
			ImageMemoryBarrier.subresourceRange.levelCount = 1;
			ImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
			ImageMemoryBarrier.subresourceRange.layerCount = 1;
			ImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			ImageMemoryBarrier.dstAccessMask = VK_ACCESS_NONE;
			vkCmdPipelineBarrier(
				VulkanObjects->DrawCmdBuffers[CurrentBuffer],
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0,
				0,
				NULL,
				0,
				NULL,
				1,
				&ImageMemoryBarrier);
		}

		THROW_ON_FAIL_VK(vkEndCommandBuffer(VulkanObjects->DrawCmdBuffers[CurrentBuffer]));

		{
			const VkPipelineStageFlags waitPipelineStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			VkSubmitInfo SubmitInfo = { 0 };
			SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			SubmitInfo.waitSemaphoreCount = 1;
			SubmitInfo.pWaitSemaphores = &VulkanObjects->PresentCompleteSemaphores[CurrentBuffer];
			SubmitInfo.pWaitDstStageMask = &waitPipelineStage;
			SubmitInfo.commandBufferCount = 1;
			SubmitInfo.pCommandBuffers = &VulkanObjects->DrawCmdBuffers[CurrentBuffer];
			SubmitInfo.signalSemaphoreCount = 1;
			SubmitInfo.pSignalSemaphores = &VulkanObjects->RenderCompleteSemaphores[CurrentImageIndex];
			THROW_ON_FAIL_VK(vkQueueSubmit(VulkanObjects->Queue, 1, &SubmitInfo, VulkanObjects->WaitFences[CurrentBuffer]));
		}

		{
			VkPresentInfoKHR PresentInfo = { 0 };
			PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			PresentInfo.waitSemaphoreCount = 1;
			PresentInfo.pWaitSemaphores = &VulkanObjects->RenderCompleteSemaphores[CurrentImageIndex];
			PresentInfo.swapchainCount = 1;
			PresentInfo.pSwapchains = &VulkanObjects->swapChain.SwapChain;
			PresentInfo.pImageIndices = &CurrentImageIndex;
			THROW_ON_FAIL_VK(vkQueuePresentKHR(VulkanObjects->Queue, &PresentInfo));
		}

		CurrentBuffer = (CurrentBuffer + 1) % MAX_CONCURRENT_FRAMES;

		float FrameTimer = (float)MovementFactor / 12;

		if (VulkanObjects->Camera.CameraType == CAMERA_TYPE_FIRST_PERSON)
		{
			if (Keys.Left || Keys.Right || Keys.Up || Keys.Down)
			{
				vec3 CamFront;
				CamFront[0] = -cos(glm_rad(VulkanObjects->Camera.Rotation[0])) * sin(glm_rad(VulkanObjects->Camera.Rotation[1]));
				CamFront[1] = sin(glm_rad(VulkanObjects->Camera.Rotation[0]));
				CamFront[2] = cos(glm_rad(VulkanObjects->Camera.Rotation[0])) * cos(glm_rad(VulkanObjects->Camera.Rotation[1]));

				glm_normalize(CamFront);

				float MoveSpeed = FrameTimer * CAMERA_MOVEMENT_SPEED;

				if (Keys.Up)
				{
					vec3 Temp;
					glm_vec3_scale(CamFront, MoveSpeed, Temp);
					glm_vec3_add(VulkanObjects->Camera.Position, Temp, VulkanObjects->Camera.Position);
				}
				if (Keys.Down)
				{
					vec3 Temp;
					glm_vec3_scale(CamFront, MoveSpeed, Temp);
					glm_vec3_sub(VulkanObjects->Camera.Position, Temp, VulkanObjects->Camera.Position);
				}
				if (Keys.Left)
				{
					vec3 Temp;
					glm_vec3_cross(CamFront, (vec3) { 0.0f, 1.0f, 0.0f }, Temp);
					glm_vec3_normalize(Temp);
					glm_vec3_scale(Temp, MoveSpeed, Temp);
					glm_vec3_sub(VulkanObjects->Camera.Position, Temp, VulkanObjects->Camera.Position);
				}
				if (Keys.Right)
				{
					vec3 Temp;
					glm_vec3_cross(CamFront, (vec3) { 0.0f, 1.0f, 0.0f }, Temp);
					glm_vec3_normalize(Temp);
					glm_vec3_scale(Temp, MoveSpeed, Temp);
					glm_vec3_add(VulkanObjects->Camera.Position, Temp, VulkanObjects->Camera.Position);
				}
			}
		}

		updateViewMatrix(&VulkanObjects->Camera);

		if (!Paused)
		{
			VulkanObjects->Timer += TIMER_SPEED * FrameTimer;
			if (VulkanObjects->Timer > 1.0)
			{
				VulkanObjects->Timer -= 1.0f;
			}
		}
	}
	break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, uMsg, wParam, lParam);
	}

	return 0;
}
