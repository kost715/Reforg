//! How the night vision image gets into the frame.
enum GZW_ENightVisionComposition
{
	//! Second camera -> render texture -> image with a circular alpha mask. Target design:
	//! the main camera is never touched, so everything outside the aperture is the ordinary night frame.
	MASKED_PIP,
	//! Second camera shown as a plain square, no mask. Diagnostic: proves that the second camera renders
	//! and that its post-process stays out of the main frame.
	SQUARE_PIP,
	//! HDR "Scope" vignette applied to the main camera. Legacy path kept for stage A of the design document.
	FULLSCREEN_SCOPE
}

[ComponentEditorProps(category: "GZW/Night Vision", description: "Standalone NVG test. N: toggle, Left Shift + =/-: gain.")]
class GZW_NightVisionTestComponentClass : ScriptComponentClass
{
}

//! Local, client-side night vision prototype.
//! Nothing here is replicated: every client drives its own camera and its own widgets.
class GZW_NightVisionTestComponent : ScriptComponent
{
	//------------------------------------------------------------------------------------------------
	// Composition
	[Attribute("0", UIWidgets.ComboBox, "How the night vision image is composited into the frame", "", ParamEnumArray.FromEnum(GZW_ENightVisionComposition), category: "Composition")]
	protected GZW_ENightVisionComposition m_eComposition;

	[Attribute("12", UIWidgets.Slider, "World camera slot used by the night vision camera. Must differ from the main camera and from PiP scopes (vanilla scopes use 8)", params: "1 31 1", category: "Composition")]
	protected int m_iCameraIndex;

	[Attribute("1", UIWidgets.Slider, "Resolution scale of the night vision render", params: "0.25 1 0.05", category: "Composition")]
	protected float m_fResolutionScale;

	[Attribute("0", UIWidgets.CheckBox, "Flip the night vision image vertically (use if the render texture shows upside down)", category: "Composition")]
	protected bool m_bFlipVertical;

	[Attribute("0", UIWidgets.CheckBox, "Invert the aperture mask (use if the image shows outside the circle instead of inside)", category: "Composition")]
	protected bool m_bInvertMask;

	//------------------------------------------------------------------------------------------------
	// Optics
	[Attribute("{C6E5B3A91D7F4082}Assets/NVG/data/GZW_Aperture_Radial.edds", UIWidgets.ResourceNamePicker, "Radial gradient used as the aperture alpha mask: 0 in the centre, 1 at the edge", "edds", category: "Optics")]
	protected ResourceName m_sMaskTexture;

	[Attribute("0.684", UIWidgets.Slider, "Aperture diameter as a fraction of the screen height", params: "0.2 1.2 0.001", category: "Optics")]
	protected float m_fApertureDiameter;

	[Attribute("0.063", UIWidgets.Slider, "Width of the soft edge as a fraction of the aperture radius", params: "0 0.5 0.001", category: "Optics")]
	protected float m_fEdgeSoftness;

	//------------------------------------------------------------------------------------------------
	// Image
	[Attribute("{B81D4E6A2F9C7350}Assets/NVG/GZW_PVS14_PiP_HDR.emat", UIWidgets.ResourceNamePicker, "HDR material of the night vision camera (no vignette: the widget mask shapes the aperture)", "emat", category: "Image")]
	protected ResourceName m_sNightVisionMaterial;

	[Attribute("{897D7A5D34FC427D}Assets/NVG/GZW_PVS14_Grain.emat", UIWidgets.ResourceNamePicker, "Film grain material", "emat", category: "Image")]
	protected ResourceName m_sGrainMaterial;

	[Attribute("{FC3F883230814A34}Assets/NVG/GZW_PVS14_HDR.emat", UIWidgets.ResourceNamePicker, "HDR material with the Scope vignette. Used by FULLSCREEN_SCOPE only", "emat", category: "Image")]
	protected ResourceName m_sScopeMaterial;

	[Attribute("1", UIWidgets.CheckBox, "Night vision camera copies the exposure of the main camera, the way vanilla PiP scopes do. Off: it runs its own auto exposure", category: "Image")]
	protected bool m_bFollowMainExposure;

	//------------------------------------------------------------------------------------------------
	// Gain
	[Attribute("0.7", UIWidgets.Slider, "Exposure change per gain step, in EV stops", params: "0 2 0.05", category: "Gain")]
	protected float m_fGainStep;

	[Attribute("-2", UIWidgets.Slider, "Lowest gain step", params: "-8 0 1", category: "Gain")]
	protected int m_iGainMin;

	[Attribute("4", UIWidgets.Slider, "Highest gain step", params: "0 8 1", category: "Gain")]
	protected int m_iGainMax;

	//------------------------------------------------------------------------------------------------
	// Test scene
	[Attribute("1", UIWidgets.Slider, "Test helper: time of day (hours) forced a few seconds after start. Negative: leave the time alone", params: "-1 24 0.25", category: "Test scene")]
	protected float m_fForcedTimeOfDay;

	//------------------------------------------------------------------------------------------------
	protected static const string ACTION_TOGGLE = "GZW_NVG_Toggle";
	protected static const string ACTION_GAIN_UP = "GZW_NVG_GainUp";
	protected static const string ACTION_GAIN_DOWN = "GZW_NVG_GainDown";

	//! Slots of the stock world post-process chain, same ones the vanilla night scope swaps.
	protected static const int HDR_PRIORITY = 16;
	protected static const int GRAIN_PRIORITY = 19;
	protected static const ResourceName STOCK_HDR_MATERIAL = "{9DEECCABE8357209}Common/Postprocess/HDR.emat";
	protected static const ResourceName STOCK_GRAIN_MATERIAL = "{BA5DE2A5962C178C}Common/Postprocess/NightNoise.emat";

	//! Part of the mask radius that carries the aperture and its soft edge; the rest is a fully transparent margin.
	protected static const float MASK_USABLE_RADIUS = 0.96;
	//! Below the HUD, above the world.
	protected static const int Z_ORDER = -100;
	protected static const int FORCED_TIME_DELAY_MS = 3000;

	protected static const float FALLBACK_FOV = 74;
	protected static const float FALLBACK_NEAR_PLANE = 0.05;
	protected static const float FALLBACK_FAR_PLANE = 4000;

	protected bool m_bListening;
	protected bool m_bEnabled;
	protected int m_iGain;
	//! Composition captured when the device was switched on, so editing the attribute cannot orphan an effect.
	protected GZW_ENightVisionComposition m_eActiveComposition;

	//! FULLSCREEN_SCOPE: main camera that currently carries the effect.
	protected int m_iScopeCameraId = -1;

	protected Widget m_wRoot;
	protected RTTextureWidget m_wRenderTexture;
	protected RenderTargetWidget m_wRenderTarget;
	protected ImageWidget m_wAperture;
	protected int m_iLayoutWidth;
	protected int m_iLayoutHeight;
	//! Side of the night vision square as a fraction of the screen height.
	protected float m_fBoxFraction;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		// No camera, UI or input on a dedicated server or in the World Editor.
		if (System.IsConsoleApp() || SCR_Global.IsEditMode(owner))
			return;

		SetEventMask(owner, EntityEvent.INIT);
		owner.SetFlags(EntityFlags.ACTIVE);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
		{
			inputManager.AddActionListener(ACTION_TOGGLE, EActionTrigger.DOWN, OnToggleAction);
			inputManager.AddActionListener(ACTION_GAIN_UP, EActionTrigger.DOWN, OnGainUpAction);
			inputManager.AddActionListener(ACTION_GAIN_DOWN, EActionTrigger.DOWN, OnGainDownAction);
			m_bListening = true;
		}

		// The game mode sets its own time on start, so the override has to come after it.
		if (m_fForcedTimeOfDay >= 0)
			GetGame().GetCallqueue().CallLater(ForceTimeOfDay, FORCED_TIME_DELAY_MS, false);

		Print("[GZW NVG] Ready. N: toggle, Left Shift + =/-: gain.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		SwitchOff();

		ArmaReforgerScripted game = GetGame();
		if (game)
		{
			game.GetCallqueue().Remove(ForceTimeOfDay);

			InputManager inputManager = game.GetInputManager();
			if (m_bListening && inputManager)
			{
				inputManager.RemoveActionListener(ACTION_TOGGLE, EActionTrigger.DOWN, OnToggleAction);
				inputManager.RemoveActionListener(ACTION_GAIN_UP, EActionTrigger.DOWN, OnGainUpAction);
				inputManager.RemoveActionListener(ACTION_GAIN_DOWN, EActionTrigger.DOWN, OnGainDownAction);
			}
		}

		m_bListening = false;

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Runs only while the device is on.
	override void EOnPostFrame(IEntity owner, float timeSlice)
	{
		if (!m_bEnabled)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		if (m_eActiveComposition == GZW_ENightVisionComposition.FULLSCREEN_SCOPE)
			UpdateScope(world);
		else
			UpdatePiP(world);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnToggleAction(float value, EActionTrigger reason)
	{
		if (m_bEnabled)
			SwitchOff();
		else
			SwitchOn();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnGainUpAction(float value, EActionTrigger reason)
	{
		ChangeGain(1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnGainDownAction(float value, EActionTrigger reason)
	{
		ChangeGain(-1);
	}

	//------------------------------------------------------------------------------------------------
	protected void SwitchOn()
	{
		if (m_bEnabled)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		int mainCameraId = world.GetCurrentCameraId();
		if (mainCameraId < 0)
			return;

		m_iGain = 0;
		m_eActiveComposition = m_eComposition;

		bool enabled;
		if (m_eActiveComposition == GZW_ENightVisionComposition.FULLSCREEN_SCOPE)
			enabled = EnableScope(world, mainCameraId);
		else
			enabled = EnablePiP(world, mainCameraId);

		if (!enabled)
			return;

		m_bEnabled = true;
		SetEventMask(GetOwner(), EntityEvent.POSTFRAME);

		PrintFormat("[GZW NVG] Enabled (%1). Gain step: 0.", typename.EnumToString(GZW_ENightVisionComposition, m_eActiveComposition));
	}

	//------------------------------------------------------------------------------------------------
	protected void SwitchOff()
	{
		if (!m_bEnabled)
			return;

		m_bEnabled = false;
		m_iGain = 0;

		GenericEntity owner = GetOwner();
		if (owner)
			ClearEventMask(owner, EntityEvent.POSTFRAME);

		BaseWorld world;
		ArmaReforgerScripted game = GetGame();
		if (game)
			world = game.GetWorld();

		if (m_eActiveComposition == GZW_ENightVisionComposition.FULLSCREEN_SCOPE)
			DisableScope(world);
		else
			DisablePiP(world);

		Print("[GZW NVG] Disabled.");
	}

	//------------------------------------------------------------------------------------------------
	protected void ChangeGain(int delta)
	{
		if (!m_bEnabled)
			return;

		int gain = Math.ClampInt(m_iGain + delta, m_iGainMin, m_iGainMax);
		if (gain == m_iGain)
			return;

		m_iGain = gain;

		// The PiP camera picks the new value up on the next frame; the scope path has no per-frame exposure work.
		if (m_eActiveComposition == GZW_ENightVisionComposition.FULLSCREEN_SCOPE && m_iScopeCameraId >= 0)
		{
			BaseWorld world = GetGame().GetWorld();
			if (world)
				world.AdjustCameraEV(m_iScopeCameraId, GetGainEV());
		}

		PrintFormat("[GZW NVG] Gain step: %1 (%2 EV)", m_iGain, GetGainEV());
	}

	//------------------------------------------------------------------------------------------------
	//! Gain is always derived from the step counter, never from an already modified exposure, so it cannot accumulate.
	protected float GetGainEV()
	{
		return m_iGain * m_fGainStep;
	}

	//------------------------------------------------------------------------------------------------
	// Picture-in-picture composition
	//------------------------------------------------------------------------------------------------
	protected bool EnablePiP(notnull BaseWorld world, int mainCameraId)
	{
		if (m_iCameraIndex == mainCameraId)
		{
			PrintFormat("[GZW NVG] Camera slot %1 is the main camera. Pick another 'Camera Index'.", m_iCameraIndex, level: LogLevel.ERROR);
			return false;
		}

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		// Camera first, so the very first rendered frame is already correct.
		world.SetCameraType(m_iCameraIndex, CameraType.PERSPECTIVE);
		world.SetCameraLensFlareSet(m_iCameraIndex, CameraLensFlareSetType.FirstPerson, string.Empty);
		world.SetCameraPostProcessEffect(m_iCameraIndex, HDR_PRIORITY, PostProcessEffectType.HDR, m_sNightVisionMaterial);
		world.SetCameraPostProcessEffect(m_iCameraIndex, GRAIN_PRIORITY, PostProcessEffectType.FilmGrain, m_sGrainMaterial);

		if (!CreatePiPWidgets(workspace, world))
		{
			DisablePiP(world);
			Print("[GZW NVG] Could not create the night vision widgets.", LogLevel.ERROR);
			return false;
		}

		UpdatePiP(world);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool CreatePiPWidgets(notnull WorkspaceWidget workspace, notnull BaseWorld world)
	{
		m_wRoot = workspace.CreateWidget(WidgetType.FrameWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.FromInt(Color.WHITE), Z_ORDER);
		if (!m_wRoot)
			return false;

		StretchToParent(m_wRoot);

		Widget renderTargetParent = m_wRoot;
		if (m_eActiveComposition == GZW_ENightVisionComposition.MASKED_PIP)
		{
			// Renders its children into a texture instead of onto the screen.
			m_wRenderTexture = RTTextureWidget.Cast(workspace.CreateWidget(WidgetType.RTTextureWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.FromInt(Color.WHITE), 0, m_wRoot));
			if (!m_wRenderTexture)
				return false;

			renderTargetParent = m_wRenderTexture;
		}

		m_wRenderTarget = RenderTargetWidget.Cast(workspace.CreateWidget(WidgetType.RenderTargetWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.FromInt(Color.WHITE), 0, renderTargetParent));
		if (!m_wRenderTarget)
			return false;

		if (m_wRenderTexture)
			StretchToParent(m_wRenderTarget);

		m_wRenderTarget.SetWorld(world, m_iCameraIndex);
		m_wRenderTarget.SetResolutionScale(m_fResolutionScale, m_fResolutionScale);

		if (m_wRenderTexture)
		{
			m_wAperture = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.FromInt(Color.WHITE), 1, m_wRoot));
			if (!m_wAperture)
				return false;

			if (m_bFlipVertical)
				m_wAperture.SetFlags(WidgetFlags.FLIPV);

			m_wAperture.SetImageTexture(0, m_wRenderTexture);
			m_wAperture.SetImage(0);

			if (!m_wAperture.LoadMaskTexture(m_sMaskTexture))
				PrintFormat("[GZW NVG] Aperture mask '%1' did not load: the image will show as an unmasked square. Reimport the PNG in Workbench.", m_sMaskTexture, level: LogLevel.WARNING);

			if (m_bInvertMask)
				m_wAperture.SetMaskMode(ImageMaskMode.INVERTED);
			else
				m_wAperture.SetMaskMode(ImageMaskMode.REGULAR);
		}

		// Force the first layout pass.
		m_iLayoutWidth = 0;
		m_iLayoutHeight = 0;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void DisablePiP(BaseWorld world)
	{
		if (m_wRoot)
			m_wRoot.RemoveFromHierarchy();

		m_wRoot = null;
		m_wRenderTexture = null;
		m_wRenderTarget = null;
		m_wAperture = null;

		if (!world)
			return;

		// The main camera was never modified, so only the night vision slot needs cleaning.
		world.SetCameraPostProcessEffect(m_iCameraIndex, HDR_PRIORITY, PostProcessEffectType.None, string.Empty);
		world.SetCameraPostProcessEffect(m_iCameraIndex, GRAIN_PRIORITY, PostProcessEffectType.None, string.Empty);
		world.SetCameraLensFlareSet(m_iCameraIndex, CameraLensFlareSetType.None, string.Empty);
		world.SetCameraHDRBrightness(m_iCameraIndex, -1);
		world.AdjustCameraEV(m_iCameraIndex, 0);
	}

	//------------------------------------------------------------------------------------------------
	//! Keeps the night vision camera glued to whatever camera the player currently looks through.
	protected void UpdatePiP(notnull BaseWorld world)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (workspace)
			UpdatePiPLayout(workspace);

		vector transform[4];
		float fov = FALLBACK_FOV;
		float nearPlane = FALLBACK_NEAR_PLANE;
		float farPlane = FALLBACK_FAR_PLANE;

		CameraBase camera;
		CameraManager cameraManager = GetGame().GetCameraManager();
		if (cameraManager)
			camera = cameraManager.CurrentCamera();

		if (camera)
		{
			camera.GetWorldCameraTransform(transform);
			fov = camera.GetVerticalFOV();
			nearPlane = camera.GetNearPlane();
			farPlane = camera.GetFarPlane();
		}
		else
		{
			world.GetCurrentCamera(transform);
		}

		// The square covers m_fBoxFraction of the screen height around the screen centre. A perspective
		// projection is linear in tan(angle), so a camera with this FOV sees exactly what the main camera
		// shows inside that square: the image lines up with the periphery without any UV cropping.
		float halfFov = Math.Clamp(fov, 1, 170) * 0.5 * Math.DEG2RAD;
		float pipFov = 2 * Math.RAD2DEG * Math.Atan2(m_fBoxFraction * Math.Tan(halfFov), 1);

		world.SetCameraEx(m_iCameraIndex, transform);
		world.SetCameraVerticalFOV(m_iCameraIndex, pipFov);
		world.SetCameraNearPlane(m_iCameraIndex, nearPlane);
		world.SetCameraFarPlane(m_iCameraIndex, farPlane);

		UpdatePiPExposure(world);
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdatePiPExposure(notnull BaseWorld world)
	{
		float mainExposure = -1;
		if (m_bFollowMainExposure)
			mainExposure = world.GetCameraHDRBrightness(world.GetCurrentCameraId());

		if (mainExposure > 0)
		{
			// Manual exposure: the main camera's own, untouched value shifted by the gain in stops.
			world.AdjustCameraEV(m_iCameraIndex, 0);
			world.SetCameraHDRBrightness(m_iCameraIndex, mainExposure * Math.Pow(2, GetGainEV()));
		}
		else
		{
			// Own auto exposure, gain as an EV offset on top of it.
			world.SetCameraHDRBrightness(m_iCameraIndex, -1);
			world.AdjustCameraEV(m_iCameraIndex, GetGainEV());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Sizes the square from the screen height, so the aperture stays round on any aspect ratio.
	protected void UpdatePiPLayout(notnull WorkspaceWidget workspace)
	{
		int screenWidth = workspace.GetWidth();
		int screenHeight = workspace.GetHeight();
		if (screenHeight <= 0)
			return;

		if (screenWidth == m_iLayoutWidth && screenHeight == m_iLayoutHeight)
			return;

		m_iLayoutWidth = screenWidth;
		m_iLayoutHeight = screenHeight;

		float diameter = Math.Clamp(m_fApertureDiameter, 0.1, 1.5);
		float softness = Math.Clamp(m_fEdgeSoftness, 0, 0.5);

		// The square holds the aperture, its soft edge and the transparent margin of the mask.
		float boxFraction = diameter * (1 + softness) / MASK_USABLE_RADIUS;
		float boxPixels = Math.Round(screenHeight * boxFraction);
		float boxSize = workspace.DPIUnscale(boxPixels);
		m_fBoxFraction = boxPixels / screenHeight;

		if (m_wRenderTexture)
			PlaceCentered(m_wRenderTexture, boxSize);
		else if (m_wRenderTarget)
			PlaceCentered(m_wRenderTarget, boxSize);

		if (m_wAperture)
		{
			PlaceCentered(m_wAperture, boxSize);

			// Mask value is the distance from the centre: below "progress" the image is opaque,
			// above "progress + transition" it is fully transparent.
			m_wAperture.SetMaskProgress(diameter / boxFraction);
			m_wAperture.SetMaskTransitionWidth(diameter * softness / boxFraction);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void StretchToParent(notnull Widget widget)
	{
		FrameSlot.SetAnchorMin(widget, 0, 0);
		FrameSlot.SetAnchorMax(widget, 1, 1);
		FrameSlot.SetOffsets(widget, 0, 0, 0, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected void PlaceCentered(notnull Widget widget, float size)
	{
		FrameSlot.SetAnchorMin(widget, 0.5, 0.5);
		FrameSlot.SetAnchorMax(widget, 0.5, 0.5);
		FrameSlot.SetAlignment(widget, 0.5, 0.5);
		FrameSlot.SetPos(widget, 0, 0);
		FrameSlot.SetSize(widget, size, size);
	}

	//------------------------------------------------------------------------------------------------
	// Legacy full-screen scope composition
	//------------------------------------------------------------------------------------------------
	protected bool EnableScope(notnull BaseWorld world, int cameraId)
	{
		m_iScopeCameraId = cameraId;
		world.SetCameraPostProcessEffect(cameraId, HDR_PRIORITY, PostProcessEffectType.HDR, m_sScopeMaterial);
		world.SetCameraPostProcessEffect(cameraId, GRAIN_PRIORITY, PostProcessEffectType.FilmGrain, m_sGrainMaterial);
		world.AdjustCameraEV(cameraId, GetGainEV());
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Puts the stock materials back, the same way the vanilla night scope does.
	protected void DisableScope(BaseWorld world)
	{
		int cameraId = m_iScopeCameraId;
		m_iScopeCameraId = -1;

		if (!world || cameraId < 0)
			return;

		world.SetCameraPostProcessEffect(cameraId, HDR_PRIORITY, PostProcessEffectType.HDR, STOCK_HDR_MATERIAL);
		world.SetCameraPostProcessEffect(cameraId, GRAIN_PRIORITY, PostProcessEffectType.FilmGrain, STOCK_GRAIN_MATERIAL);
		world.AdjustCameraEV(cameraId, 0);
	}

	//------------------------------------------------------------------------------------------------
	//! Moves the effect when the player switches to another camera, so it never stays behind on the old one.
	protected void UpdateScope(notnull BaseWorld world)
	{
		int cameraId = world.GetCurrentCameraId();
		if (cameraId < 0 || cameraId == m_iScopeCameraId)
			return;

		DisableScope(world);
		EnableScope(world, cameraId);
	}

	//------------------------------------------------------------------------------------------------
	// Test scene helper
	//------------------------------------------------------------------------------------------------
	protected void ForceTimeOfDay()
	{
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return;

		TimeAndWeatherManagerEntity manager = world.GetTimeAndWeatherManager();
		if (!manager)
		{
			Print("[GZW NVG] No time and weather manager in this world: set the time of day by hand.", LogLevel.WARNING);
			return;
		}

		// Authority only: on a client of a remote server this is refused and the time stays as it is.
		if (!manager.SetTimeOfTheDay(m_fForcedTimeOfDay, true))
			return;

		manager.SetIsDayAutoAdvanced(false);
		PrintFormat("[GZW NVG] Test scene time forced to %1 h.", m_fForcedTimeOfDay);
	}
}
