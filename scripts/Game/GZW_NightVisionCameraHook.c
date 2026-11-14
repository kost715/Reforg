//! Hands the player's camera transform to the night vision device the moment the camera handler has computed it.
//! Script entities are updated before this point in the frame, so nothing they read from the camera is current.
modded class SCR_CharacterCameraHandlerComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnAfterCameraUpdate(float pDt, bool pIsKeyframe, inout vector transformMS[4], inout vector transformWS[4])
	{
		super.OnAfterCameraUpdate(pDt, pIsKeyframe, transformMS, transformWS);

		GZW_NightVisionTestComponent device = GZW_NightVisionTestComponent.GetActiveDevice();
		if (!device)
			return;

		// Only the local player's character drives the view, and only while its camera is the active one.
		if (GetOwner() != SCR_PlayerController.GetLocalControlledEntity())
			return;

		CameraManager cameraManager = GetGame().GetCameraManager();
		if (!cameraManager || !PlayerCamera.Cast(cameraManager.CurrentCamera()))
			return;

		device.OnPlayerCameraUpdated(transformWS);
	}
}
