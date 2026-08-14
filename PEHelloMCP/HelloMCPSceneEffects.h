#pragma once

#include "HelloMCPCommon.h"
#include <GL/GL.h>

#pragma comment(lib, "opengl32.lib")

namespace hello_mcp { namespace detail {

class SceneEffectService {
public:
	json MakeSceneCacheState(const std::string& effectName) {
		if (g_pe_scene_cache.contains(effectName)) {
			return g_pe_scene_cache[effectName];
		}
		return json::object();
	}

	void CacheSceneState(const std::string& effectName, const json& state) {
		g_pe_scene_cache[effectName] = state;
	}

	json make_pe_result(bool success, const std::string& action,
		const std::string& effectName, const std::string& target) {
		json r;
		r["success"] = success;
		r["action"] = action;
		r["effect_name"] = effectName;
		r["target"] = target;
		r["state"] = nullptr;
		return r;
	}

	bool RequireDataObject(const json& payload, json& response) {
		if (!payload.contains("data") || !payload["data"].is_object()) {
			response["success"] = false;
			response["error"] = "PE apply requires object field: data";
			return false;
		}
		return true;
	}

	json make_frame_result(bool success, const std::string& path, const std::string& error = "") {
		json frame;
		frame["success"] = success;
		frame["path"] = path;
		frame["format"] = "png";
		frame["source"] = "engine_framebuffer";
		if (!error.empty()) {
			frame["error"] = error;
		}
		return frame;
	}

	bool EnsureGdiPlus(std::string& errorMessage) {
		static std::once_flag initFlag;
		static bool initialized = false;
		static std::string initError;
		static ULONG_PTR gdiplusToken = 0;

		std::call_once(initFlag, []() {
			Gdiplus::GdiplusStartupInput startupInput;
			Gdiplus::Status status = Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr);
			if (status == Gdiplus::Ok) {
				initialized = true;
				return;
			}

			initError = "GDI+ startup failed: " + std::to_string(static_cast<int>(status));
		});

		if (!initialized) {
			errorMessage = initError.empty() ? "GDI+ startup failed" : initError;
			return false;
		}
		return true;
	}

	int GetPngEncoderClsid(CLSID* pClsid) {
		UINT num = 0;
		UINT size = 0;
		Gdiplus::GetImageEncodersSize(&num, &size);
		if (size == 0) {
			return -1;
		}

		std::vector<BYTE> buffer(size);
		Gdiplus::ImageCodecInfo* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
		Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

		for (UINT i = 0; i < num; ++i) {
			if (wcscmp(pImageCodecInfo[i].MimeType, L"image/png") == 0) {
				*pClsid = pImageCodecInfo[i].Clsid;
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	std::string SanitizeFrameLabel(const std::string& label) {
		std::string sanitized;
		sanitized.reserve(label.size());
		for (unsigned char ch : label) {
			if (std::isalnum(ch) || ch == '_' || ch == '-') {
				sanitized.push_back(static_cast<char>(ch));
			}
			else {
				sanitized.push_back('_');
			}
		}

		if (sanitized.empty()) {
			return "frame";
		}
		return sanitized;
	}

	bool SaveBitmapToPng(HBITMAP hBitmap, const std::wstring& outputPath, std::string& errorMessage) {
		if (hBitmap == nullptr) {
			errorMessage = "bitmap handle is null";
			return false;
		}

		std::string gdiplusError;
		if (!EnsureGdiPlus(gdiplusError)) {
			errorMessage = gdiplusError;
			return false;
		}

		CLSID pngClsid;
		if (GetPngEncoderClsid(&pngClsid) < 0) {
			errorMessage = "PNG encoder not found";
			return false;
		}

		Gdiplus::Bitmap bitmap(hBitmap, nullptr);
		Gdiplus::Status status = bitmap.Save(outputPath.c_str(), &pngClsid, nullptr);
		if (status != Gdiplus::Ok) {
			errorMessage = "bitmap save failed: " + std::to_string(static_cast<int>(status));
			return false;
		}

		return true;
	}

	HBITMAP CreateBitmapFromOpenGLRgb(const std::vector<unsigned char>& pixels,
		int width, int height, std::string& errorMessage) {
		BITMAPINFO bitmapInfo = {};
		bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapInfo.bmiHeader.biWidth = width;
		bitmapInfo.bmiHeader.biHeight = height;
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 24;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;

		void* bitmapBits = nullptr;
		HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS,
			&bitmapBits, nullptr, 0);
		if (bitmap == nullptr || bitmapBits == nullptr) {
			if (bitmap != nullptr) {
				DeleteObject(bitmap);
			}
			errorMessage = "failed to create bitmap for engine framebuffer";
			return nullptr;
		}

		const size_t sourceStride = static_cast<size_t>(width) * 3;
		const size_t targetStride = (sourceStride + 3) & ~static_cast<size_t>(3);
		unsigned char* target = static_cast<unsigned char*>(bitmapBits);
		for (int y = 0; y < height; ++y) {
			const unsigned char* sourceRow = pixels.data() + static_cast<size_t>(y) * sourceStride;
			unsigned char* targetRow = target + static_cast<size_t>(y) * targetStride;
			for (int x = 0; x < width; ++x) {
				targetRow[x * 3] = sourceRow[x * 3 + 2];
				targetRow[x * 3 + 1] = sourceRow[x * 3 + 1];
				targetRow[x * 3 + 2] = sourceRow[x * 3];
			}
		}
		return bitmap;
	}

	json cc_captureCurrentFramePng(const std::string& effectName) {
		HWND renderWindow = reinterpret_cast<HWND>(
			FxPluginMgr_GetData(nullptr, "HWND_3DView"));
		if (renderWindow == nullptr || !IsWindow(renderWindow)) {
			return make_frame_result(false, "", "engine render view is not available");
		}

		HWND previousWindow = VsGetActiveWindow();
		if (previousWindow != renderWindow) {
			VsSetActiveWindow(renderWindow);
		}

		if (wglGetCurrentContext() == nullptr) {
			if (previousWindow != renderWindow) {
				VsSetActiveWindow(previousWindow);
			}
			return make_frame_result(false, "", "engine OpenGL context is not available");
		}

		GLint viewport[4] = { 0, 0, 0, 0 };
		glGetIntegerv(GL_VIEWPORT, viewport);
		const int width = viewport[2];
		const int height = viewport[3];
		if (width <= 0 || height <= 0) {
			if (previousWindow != renderWindow) {
				VsSetActiveWindow(previousWindow);
			}
			return make_frame_result(false, "", "invalid engine framebuffer size");
		}

		const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
		if (pixelCount > (static_cast<size_t>(-1) / 3)) {
			if (previousWindow != renderWindow) {
				VsSetActiveWindow(previousWindow);
			}
			return make_frame_result(false, "", "engine framebuffer is too large");
		}

		std::vector<unsigned char> pixels(pixelCount * 3);
		for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadBuffer(GL_FRONT);
		glReadPixels(viewport[0], viewport[1], width, height,
			GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
		const GLenum captureError = glGetError();
		if (previousWindow != renderWindow) {
			VsSetActiveWindow(previousWindow);
		}
		if (captureError != GL_NO_ERROR) {
			return make_frame_result(false, "",
				"engine framebuffer read failed: OpenGL error " +
				std::to_string(static_cast<unsigned int>(captureError)));
		}

		std::string bitmapError;
		HBITMAP bitmap = CreateBitmapFromOpenGLRgb(pixels, width, height, bitmapError);
		if (bitmap == nullptr) {
			return make_frame_result(false, "", bitmapError);
		}

		try {
			fs::path outputDir = fs::temp_directory_path() / "hello_mcp_frames";
			if (!fs::exists(outputDir)) {
				fs::create_directories(outputDir);
			}

			std::string safeLabel = SanitizeFrameLabel(effectName);
			std::string fileName = safeLabel + "_" + std::to_string(static_cast<long long>(std::time(nullptr))) +
				"_" + std::to_string(static_cast<unsigned long long>(GetTickCount64())) + ".png";
			fs::path outputPath = outputDir / fileName;

			std::string saveError;
			bool saved = SaveBitmapToPng(bitmap, outputPath.wstring(), saveError);
			DeleteObject(bitmap);
			if (!saved) {
				return make_frame_result(false, "", saveError);
			}

			json result = make_frame_result(true, _U(outputPath.string()));
			result["width"] = width;
			result["height"] = height;
			return result;
		}
		catch (const fs::filesystem_error& e) {
			DeleteObject(bitmap);
			return make_frame_result(false, "", std::string("frame path error: ") + e.what());
		}
	}

	json FinalizeSceneEffectApplyResponse(const std::string& action, const std::string& effectName,
		const std::string& target, const std::string& message, const json& state) {
		json response = make_pe_result(true, action, effectName, target);
		response["message"] = message;
		response["state"] = state;
		return response;
	}

	json ProcessFrameCapture(const std::string& action, const json& payload) {
		const std::string label = payload.value("label", std::string("manual_capture"));
		json frame = cc_captureCurrentFramePng(label);

		json response;
		response["success"] = frame.value("success", false);
		response["action"] = action;
		response["frame"] = frame;
		response["message"] = response["success"].get<bool>() ? "frame captured" : "frame capture failed";
		if (!response["success"].get<bool>() && frame.contains("error")) {
			response["error"] = frame["error"];
		}
		return response;
	}

	json QueryAOState() {
		BOOL enabled = FALSE;
		BOOL bentNormal = FALSE;
		float factor = 0.0f;
		float radius = 0.0f;
		VsShowOptionValuei("ssao", enabled);
		VsShowOptionValuef("ssaofactor", factor);
		VsShowOptionValuef("ssaoradius", radius);
		VsShowOptionValuei("ssaobentnormal", bentNormal);

		json state;
		state["enabled"] = (enabled == TRUE);
		state["factor"] = factor;
		state["radius"] = radius;
		state["bent_normal"] = (bentNormal == TRUE);
		return state;
	}

	json ProcessAOQuery(const std::string& action, const std::string& effectName, const std::string& target) {
		json response = make_pe_result(true, action, effectName, target);
		response["message"] = "AO state queried";
		response["state"] = QueryAOState();
		return response;
	}

	json ProcessAOApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		const json& data = payload["data"];
		if (data.contains("enabled")) VsSetOptionValuei("ssao", data["enabled"].get<bool>() ? TRUE : FALSE);
		if (data.contains("factor")) VsSetOptionValuef("ssaofactor", data["factor"].get<float>());
		if (data.contains("radius")) VsSetOptionValuef("ssaoradius", data["radius"].get<float>());
		if (data.contains("bent_normal")) VsSetOptionValuei("ssaobentnormal", data["bent_normal"].get<bool>() ? TRUE : FALSE);
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "AO applied", QueryAOState());
	}

	json QueryBloomState() {
		BOOL enabled = FALSE;
		float threshold = 0.0f;
		VsShowOptionValuei("bloom", enabled);
		VsShowOptionValuef("bloomthreshold", threshold);

		json state;
		state["enabled"] = (enabled == TRUE);
		state["threshold"] = threshold;
		return state;
	}

	json ProcessBloomApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		const json& data = payload["data"];
		if (data.contains("enabled")) VsSetOptionValuei("bloom", data["enabled"].get<bool>() ? TRUE : FALSE);
		if (data.contains("threshold")) VsSetOptionValuef("bloomthreshold", data["threshold"].get<float>());
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "Bloom applied", QueryBloomState());
	}

	json QuerySSRSceneState() {
		BOOL enabled = FALSE;
		float maxDistance = 0.0f;
		VsShowOptionValuei("ssr", enabled);
		VsShowOptionValuef("ssrmaxdistance", maxDistance);

		json state;
		state["enabled"] = (enabled == TRUE);
		state["max_distance"] = maxDistance;
		return state;
	}

	json ProcessSSRSceneApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		const json& data = payload["data"];
		if (data.contains("enabled")) VsSetOptionValuei("ssr", data["enabled"].get<bool>() ? TRUE : FALSE);
		if (data.contains("max_distance")) VsSetOptionValuef("ssrmaxdistance", data["max_distance"].get<float>());
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "SSR scene applied", QuerySSRSceneState());
	}

	json QueryProbeFactorState() {
		return MakeSceneCacheState("probefactor");
	}

	json ProcessProbeFactorApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		json state = QueryProbeFactorState();
		const json& data = payload["data"];
		if (data.contains("factor")) {
			float factor = data["factor"].get<float>();
			VsSetOptionValuef("probefactor", factor);
			state["factor"] = factor;
		}

		CacheSceneState("probefactor", state);
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "Probe factor applied", state);
	}

	json QueryAirParticleDensityState() {
		return MakeSceneCacheState("airparticledensity");
	}

	json ProcessAirParticleDensityApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		json state = QueryAirParticleDensityState();
		const json& data = payload["data"];
		if (data.contains("density")) {
			float density = data["density"].get<float>();
			VsSetOptionValuef("airparticledensity", density);
			state["density"] = density;
		}

		CacheSceneState("airparticledensity", state);
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "Air particle density applied", state);
	}

	json QueryAtmosphereState() {
		BOOL enabled = FALSE;
		float density = 0.0f;
		VsShowOptionValuei("atmosphere", enabled);
		VsShowOptionValuef("atmospheredensity", density);

		json state;
		state["enabled"] = (enabled == TRUE);
		state["density"] = density;
		return state;
	}

	json ProcessAtmosphereApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		const json& data = payload["data"];
		if (data.contains("enabled")) VsSetOptionValuei("atmosphere", data["enabled"].get<bool>() ? TRUE : FALSE);
		if (data.contains("density")) VsSetOptionValuef("atmospheredensity", data["density"].get<float>());
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "Atmosphere applied", QueryAtmosphereState());
	}

	json QuerySunState() {
		BOOL enabled = FALSE;
		float exposure = 0.0f;
		float azimuth = 0.0f;
		float zenith = 0.0f;
		VsShowOptionValuei("sun", enabled);
		VsShowOptionValuef("sunexposure", exposure);
		VsShowOptionValuef("sunazimuth", azimuth);
		VsShowOptionValuef("sunzenith", zenith);

		json state;
		state["enabled"] = (enabled == TRUE);
		state["exposure"] = exposure;
		state["azimuth"] = azimuth;
		state["zenith"] = zenith;
		return state;
	}

	json ProcessSunApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		const json& data = payload["data"];
		if (data.contains("enabled")) VsSetOptionValuei("sun", data["enabled"].get<bool>() ? TRUE : FALSE);
		if (data.contains("exposure")) VsSetOptionValuef("sunexposure", data["exposure"].get<float>());
		if (data.contains("azimuth")) VsSetOptionValuef("sunazimuth", data["azimuth"].get<float>());
		if (data.contains("zenith")) VsSetOptionValuef("sunzenith", data["zenith"].get<float>());
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "Sun applied", QuerySunState());
	}

	json QueryFogState() {
		BOOL enabled = FALSE;
		float density = 0.0f;
		float height = 0.0f;
		float color[3] = { 0.0f, 0.0f, 0.0f };
		int count = 0;
		VsShowOptionValuei("fog", enabled);
		VsShowOptionValuef("fogdensity", density);
		VsShowOptionValuefv("fogcolor", color, count);
		VsShowOptionValuef("fogheight", height);

		json state;
		state["enabled"] = (enabled == TRUE);
		state["density"] = density;
		state["color"] = json::array({ color[0], color[1], color[2] });
		state["height"] = height;
		return state;
	}

	json ProcessFogApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		const json& data = payload["data"];
		if (data.contains("enabled")) VsSetOptionValuei("fog", data["enabled"].get<bool>() ? TRUE : FALSE);
		if (data.contains("density")) VsSetOptionValuef("fogdensity", data["density"].get<float>());
		if (data.contains("color")) {
			std::vector<float> color = data["color"].get<std::vector<float>>();
			if (color.size() == 3) {
				VsSetOptionValuefv("fogcolor", color.data(), 3);
			}
		}
		if (data.contains("height")) VsSetOptionValuef("fogheight", data["height"].get<float>());
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "Fog applied", QueryFogState());
	}

	json QueryPostprocessState() {
		float tone = 0.0f;
		float saturation = 0.0f;
		float contrast = 0.0f;
		VsShowOptionValuef("tone", tone);
		VsShowOptionValuef("saturation", saturation);
		VsShowOptionValuef("contrast", contrast);

		json state;
		state["tone"] = tone;
		state["saturation"] = saturation;
		state["contrast"] = contrast;
		return state;
	}

	json ProcessPostprocessApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		json response = make_pe_result(false, action, effectName, target);
		if (!RequireDataObject(payload, response)) {
			return response;
		}

		const json& data = payload["data"];
		if (data.contains("tone")) VsSetOptionValuef("tone", data["tone"].get<float>());
		if (data.contains("saturation")) VsSetOptionValuef("saturation", data["saturation"].get<float>());
		if (data.contains("contrast")) VsSetOptionValuef("contrast", data["contrast"].get<float>());
		return FinalizeSceneEffectApplyResponse(action, effectName, target, "Postprocess applied", QueryPostprocessState());
	}

	// main
	json ProcessSceneEffectQuery(const std::string& action, const std::string& effectName,
		const std::string& target) {
		if (effectName == "ssao" || effectName == "ao") return ProcessAOQuery(action, effectName, target);
		if (effectName == "bloom") {
			json response = make_pe_result(true, action, effectName, target);
			response["message"] = "Bloom state queried";
			response["state"] = QueryBloomState();
			return response;
		}
		if (effectName == "ssr_scene") {
			json response = make_pe_result(true, action, effectName, target);
			response["message"] = "SSR scene state queried";
			response["state"] = QuerySSRSceneState();
			return response;
		}
		if (effectName == "probefactor") {
			json response = make_pe_result(true, action, effectName, target);
			response["message"] = "Probe factor state queried";
			response["state"] = QueryProbeFactorState();
			return response;
		}
		if (effectName == "airparticledensity") {
			json response = make_pe_result(true, action, effectName, target);
			response["message"] = "Air particle density state queried";
			response["state"] = QueryAirParticleDensityState();
			return response;
		}
		if (effectName == "atmosphere") {
			json response = make_pe_result(true, action, effectName, target);
			response["message"] = "Atmosphere state queried";
			response["state"] = QueryAtmosphereState();
			return response;
		}
		if (effectName == "sun") {
			json response = make_pe_result(true, action, effectName, target);
			response["message"] = "Sun state queried";
			response["state"] = QuerySunState();
			return response;
		}
		if (effectName == "fog") {
			json response = make_pe_result(true, action, effectName, target);
			response["message"] = "Fog state queried";
			response["state"] = QueryFogState();
			return response;
		}
		if (effectName == "postprocess") {
			json response = make_pe_result(true, action, effectName, target);
			response["message"] = "Postprocess state queried";
			response["state"] = QueryPostprocessState();
			return response;
		}

		json response = make_pe_result(false, action, effectName, target);
		response["error"] = "Unknown scene effect: " + effectName;
		return response;
	}

	json ProcessSceneEffectApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload) {
		if (effectName == "ssao" || effectName == "ao") return ProcessAOApply(action, effectName, target, payload);
		if (effectName == "bloom") return ProcessBloomApply(action, effectName, target, payload);
		if (effectName == "ssr_scene") return ProcessSSRSceneApply(action, effectName, target, payload);
		if (effectName == "probefactor") return ProcessProbeFactorApply(action, effectName, target, payload);
		if (effectName == "airparticledensity") return ProcessAirParticleDensityApply(action, effectName, target, payload);
		if (effectName == "atmosphere") return ProcessAtmosphereApply(action, effectName, target, payload);
		if (effectName == "sun") return ProcessSunApply(action, effectName, target, payload);
		if (effectName == "fog") return ProcessFogApply(action, effectName, target, payload);
		if (effectName == "postprocess") return ProcessPostprocessApply(action, effectName, target, payload);

		json response = make_pe_result(false, action, effectName, target);
		response["error"] = "Unknown scene effect: " + effectName;
		return response;
	}
};

} }
