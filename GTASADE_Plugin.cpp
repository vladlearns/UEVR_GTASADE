#include "uevr/Plugin.hpp"
#include "uevr/API.hpp"
#include "MemoryManager.h"
#include "SettingsManager.h"
#include "CameraController.h"
#include "PlayerManager.h"
#include "WeaponManager.h"
#include "Utilities.h"
//#include <chrono>

using namespace uevr;

#define PLUGIN_LOG_ONCE(...) {\
    static bool _logged_ = false; \
    if (!_logged_) { \
        _logged_ = true; \
        API::get()->log_info(__VA_ARGS__); \
    }}

class GTASADE_Plugin : public uevr::Plugin {
private:
	MemoryManager memoryManager;
	SettingsManager settingsManager;
	CameraController cameraController;
	PlayerManager playerManager;
	WeaponManager weaponManager;


public:
	GTASADE_Plugin() : cameraController(&memoryManager, &settingsManager, &playerManager),
        weaponManager(&playerManager, &cameraController, &memoryManager, &settingsManager),
		playerManager(&settingsManager),
		memoryManager(&settingsManager){}

	void on_dllmain() override {}

	void on_dllmain_detach() override {
		ChangePluginState(false);
	}

	void on_initialize() override {
		API::get()->log_info("%s", "VR cpp mod initializing");
		settingsManager.InitSettingsManager();
		memoryManager.InitMemoryManager();
		Utilities::InitHelperClasses();
		weaponManager.HideBulletTrace();
		settingsManager.SetOrientationMethod(!settingsManager.autoOrientationMode);
	}

	void on_pre_engine_tick(API::UGameEngine* engine, float delta) override {
		PLUGIN_LOG_ONCE("Pre Engine Tick: %f", delta);
		/*auto start = std::chrono::high_resolution_clock::now();*/

		FetchRequiredValuesFromMemory();
		playerManager.FetchPlayerUObjects();
		if (!cameraController.underwaterViewFixed && playerManager.isInControl)
			cameraController.FixUnderwaterView(true);

		// We need to fetch the weapon one last time after player lost control so the plugin can correctly reset the weapon position for cutscenes.
		if (!playerManager.isInControl && playerManager.wasInControl)
			weaponManager.UpdateActualWeaponMesh();

		// Toggles the game's original instructions when going in or out of a vehicle if there's no scripted event with AimWeaponFromCar camera.
		// Then sets UEVR settings according to the vehicle type
		if ((playerManager.isInControl && playerManager.isInVehicle && memoryManager.vehicleRelatedMemoryInstructionsNoped) ||
			(playerManager.isInVehicle && cameraController.currentCameraMode != CameraController::AimWeaponFromCar && memoryManager.vehicleRelatedMemoryInstructionsNoped))
		{
			memoryManager.RestoreVehicleRelatedMemoryInstructions();
			if (settingsManager.autoPitchAndLerpForFlight && playerManager.vehicleType == PlayerManager::Plane || playerManager.vehicleType == PlayerManager::Helicopter)
			{
				settingsManager.CacheSettings();
				settingsManager.SetPitchAndLerpSettingsForFlight(false);
			}
			if (settingsManager.leftHandedMode)
				API::get()->dispatch_lua_event("playerIsInVehicle", "true");
			weaponManager.ResetShootingState();
		}
		if ((playerManager.isInControl && !playerManager.isInVehicle && !memoryManager.vehicleRelatedMemoryInstructionsNoped) ||
			(playerManager.isInVehicle && cameraController.currentCameraMode == CameraController::AimWeaponFromCar && !memoryManager.vehicleRelatedMemoryInstructionsNoped))
		{
			memoryManager.NopVehicleRelatedMemoryInstructions();
			if (settingsManager.autoPitchAndLerpForFlight)  // reset the setting to user preset
			{
				uevr::API::VR::set_decoupled_pitch_enabled(settingsManager.storedDecoupledPitch);
				settingsManager.SetPitchAndLerpSettingsForFlight(true);
			}
			if (settingsManager.leftHandedMode)
				API::get()->dispatch_lua_event("playerIsInVehicle", "false");
			weaponManager.ResetShootingState();
		}

		// Handles the VR mod state during cutscenes and various points in which the camera should be freed from VR controls.
		if (!playerManager.isInControl && playerManager.wasInControl)
		{
			if (settingsManager.debugMod) API::get()->log_info("player NOT InControl");
			settingsManager.CacheSettings();
			ChangePluginState(false);
			if (settingsManager.autoDecoupledPitchDuringCutscenes)
				uevr::API::VR::set_decoupled_pitch_enabled(true); // Force decoupled pitch during cutscenes
		}
		if (playerManager.isInControl && !playerManager.wasInControl)
		{
			if (settingsManager.debugMod) API::get()->log_info("playerIsInControl");
			ChangePluginState(true);
			if (settingsManager.autoDecoupledPitchDuringCutscenes)
				uevr::API::VR::set_decoupled_pitch_enabled(settingsManager.storedDecoupledPitch); // reset decoupled pitch after cutscenes to user preset
		}

		// Auto Orientation method, allows CJ's walking direction to be relative to the hmd orientation when on foot, 
		// switches back to the game orientation method when driving a vehicle to prevent steer lock when looking around.
		if (settingsManager.autoOrientationMode && playerManager.isInVehicle && !playerManager.wasInVehicle)
			settingsManager.SetOrientationMethod(true);
		if (settingsManager.autoOrientationMode && !playerManager.isInVehicle && playerManager.wasInVehicle)
			settingsManager.SetOrientationMethod(false);

		// Fix for the bugged Camera in the mission Catalyst
		//if (cameraController.currentCameraMode == CameraController::HelicannonFirstPerson && cameraController.previousCameraMode != CameraController::HelicannonFirstPerson)
		//	memoryManager.ToggleHeliCanonCameraModMemoryInstructions(true);
		//if (cameraController.currentCameraMode != CameraController::HelicannonFirstPerson && cameraController.previousCameraMode == CameraController::HelicannonFirstPerson)
		//	memoryManager.ToggleHeliCanonCameraModMemoryInstructions(false);

		// Toggles the game's original instructions for the camera weapon controls
		if (cameraController.currentCameraMode == CameraController::Camera && cameraController.previousCameraMode != CameraController::Camera)
			memoryManager.ToggleAllMemoryInstructions(true);
		if (cameraController.currentCameraMode != CameraController::Camera && cameraController.previousCameraMode == CameraController::Camera)
			memoryManager.ToggleAllMemoryInstructions(false);

		// Main VR functions :
		if (playerManager.isInControl)
		{
			weaponManager.UpdateActualWeaponMesh();
			if (settingsManager.debugMod) uevr::API::get()->log_info("equippedWeaponIndex");
			
			if (!playerManager.weaponWheelEnabled)
			{
				cameraController.ProcessCameraMatrix(delta);
				cameraController.ProcessHookedHeadPosition(delta);
				weaponManager.UpdateShootingState(!weaponManager.firstWeaponShotDone);
				weaponManager.ProcessAiming(!weaponManager.firstWeaponShotDone);
			}

			weaponManager.ProcessWeaponHandling(delta);
			weaponManager.ProcessWeaponVisibility();
		}
		settingsManager.UpdateSettingsIfModified();
		UpdatePreviousStates();

		//auto end = std::chrono::high_resolution_clock::now();
		//auto duration_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		//API::get()->log_info("execution time : %lld micro seconds", duration_ms.count());
		//Last test average = 85,150537634409 micro seconds
	}


	void on_post_engine_tick(API::UGameEngine* engine, float delta) override {
		PLUGIN_LOG_ONCE("Post Engine Tick: %f", delta);
	}

	void on_pre_slate_draw_window(UEVR_FSlateRHIRendererHandle renderer, UEVR_FViewportInfoHandle viewport_info) override {
		PLUGIN_LOG_ONCE("Pre Slate Draw Window");
	}

	void on_post_slate_draw_window(UEVR_FSlateRHIRendererHandle renderer, UEVR_FViewportInfoHandle viewport_info) override {
		PLUGIN_LOG_ONCE("Post Slate Draw Window");
	}

	void ChangePluginState(bool enable)
	{
		if (settingsManager.debugMod) API::get()->log_info("ChangePluginState");

		if (enable)
		{
			cameraController.camResetRequested = true;
			memoryManager.ToggleAllMemoryInstructions(false);
			memoryManager.InstallBreakpoints();
			uevr::API::UObjectHook::set_disabled(false);
			weaponManager.ResetShootingState();
		}
		else
		{
			memoryManager.RemoveBreakpoints();
			memoryManager.RemoveExceptionHandler();
			memoryManager.ToggleAllMemoryInstructions(true);
			cameraController.FixUnderwaterView(false);
			uevr::API::UObjectHook::set_disabled(true);
			playerManager.RepositionUnhookedUobjects();
			weaponManager.UnhookAndRepositionWeapon();
		}
	}

	void FetchRequiredValuesFromMemory()
	{
		if (settingsManager.debugMod) API::get()->log_info("FetchRequiredValuesFromMemory");
		playerManager.isInControl = *(reinterpret_cast<uint8_t*>(memoryManager.playerIsInControlAddress)) == 0;
		playerManager.isInVehicle = *(reinterpret_cast<uint8_t*>(memoryManager.playerIsInVehicleAddress)) > 0;
		playerManager.vehicleType = *(reinterpret_cast<PlayerManager::VehicleType*>(memoryManager.vehicleTypeAddress));
		playerManager.shootFromCarInput = *(reinterpret_cast<int*>(memoryManager.playerShootFromCarInputAddress)) == 3;
		playerManager.weaponWheelEnabled = *(reinterpret_cast<int*>(memoryManager.weaponWheelDisplayedAddress)) > 30;
		cameraController.currentCameraMode = *(reinterpret_cast<CameraController::CameraMode*>(memoryManager.cameraModeAddress));
		//cameraController.isCutscenePlaying = *(reinterpret_cast<uint8_t*>(memoryManager.cutscenePlayingAddress)) > 0;
	}

	void UpdatePreviousStates()
	{
		if (settingsManager.debugMod) API::get()->log_info("UpdatePreviousStates");

		playerManager.wasInControl = playerManager.isInControl;
		playerManager.wasInVehicle = playerManager.isInVehicle;
		cameraController.previousCameraMode = cameraController.currentCameraMode;
		//cameraController.wasCutscenePlaying = cameraController.isCutscenePlaying;
		weaponManager.previousWeaponEquipped = weaponManager.currentWeaponEquipped;
	}
};

// Actually creates the plugin. Very important that this global is created.
// The fact that it's using std::unique_ptr is not important, as long as the constructor is called in some way.
std::unique_ptr<GTASADE_Plugin> g_plugin{ new GTASADE_Plugin() };