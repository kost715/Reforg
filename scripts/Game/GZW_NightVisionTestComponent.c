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

	[Attribute("1", UIWidgets.Slider, "Resolution scale of the night vision render. Lower is cheaper and softer", params: "0.25 1 0.05", category: "Performance")]
	protected float m_fResolutionScale;

	[Attribute("0", UIWidgets.Slider, "Furthest distance the night vision camera draws, in metres. 0: same as the main camera. Lower is cheaper, but distant terrain disappears inside the aperture", params: "0 5000 50", category: "Performance")]
	protected float m_fViewDistanceLimit;

	[Attribute("0", UIWidgets.Slider, "Frame rate cap of the night vision render. 0: every frame. A cap is cheaper, but the image falls behind the periphery when the camera moves", params: "0 240 5", category: "Performance")]
	protected int m_iMaxFPS;

	[Attribute("0", UIWidgets.ComboBox, "Pixel format of the night vision render", "", ParamEnumArray.FromEnum(RenderTargetWidgetFormat), category: "Composition")]
	protected RenderTargetWidgetFormat m_eRenderFormat;

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
	[Attribute("{9DEECCABE8357209}Common/Postprocess/HDR.emat", UIWidgets.ResourceNamePicker, "HDR material of the night vision camera. Default: the game's stock HDR material", "emat", category: "Image")]
	protected ResourceName m_sNightVisionMaterial;

	[Attribute("{BA5DE2A5962C178C}Common/Postprocess/NightNoise.emat", UIWidgets.ResourceNamePicker, "Film grain material of the night vision camera. Default: the game's night noise", "emat", category: "Image")]
	protected ResourceName m_sGrainMaterial;

	[Attribute("0.15 1 0.2 1", UIWidgets.ColorPicker, "Phosphor colour: the night vision image is multiplied by it", category: "Image")]
	protected ref Color m_PhosphorColor;

	[Attribute("1", UIWidgets.Slider, "Saturation of the night vision image. The engine applies it after the phosphor tint, so 0 removes the tint and gives a white phosphor look", params: "0 1 0.05", category: "Image")]
	protected float m_fSaturation;

	[Attribute("{FC3F883230814A34}Assets/NVG/GZW_PVS14_HDR.emat", UIWidgets.ResourceNamePicker, "HDR material with the Scope vignette. Used by FULLSCREEN_SCOPE only", "emat", category: "Image")]
	protected ResourceName m_sScopeMaterial;

	[Attribute("{897D7A5D34FC427D}Assets/NVG/GZW_PVS14_Grain.emat", UIWidgets.ResourceNamePicker, "Film grain material. Used by FULLSCREEN_SCOPE only", "emat", category: "Image")]
	protected ResourceName m_sScopeGrainMaterial;

	[Attribute("1", UIWidgets.CheckBox, "Night vision camera copies the exposure of the main camera, the way vanilla PiP scopes do. Off: it runs its own auto exposure", category: "Image")]
	protected bool m_bFollowMainExposure;

	//------------------------------------------------------------------------------------------------
	// Gain
	[Attribute("3", UIWidgets.Slider, "Exposure boost of the night vision image at gain step 0, in EV stops", params: "-4 10 0.25", category: "Gain")]
	protected float m_fBaseGain;

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

	[Attribute("1", UIWidgets.CheckBox, "Test helper: debug panel with the device state, plus debug keys. Left Shift + ]: next composition, Left Shift + [: next render format, Left Shift + ;: next HDR material, Left Shift + ': night or day", category: "Test scene")]
	protected bool m_bDebug;

	//------------------------------------------------------------------------------------------------
	protected static const string ACTION_TOGGLE = "GZW_NVG_Toggle";
	protected static const string ACTION_GAIN_UP = "GZW_NVG_GainUp";
	protected static const string ACTION_GAIN_DOWN = "GZW_NVG_GainDown";
	protected static const string ACTION_NEXT_COMPOSITION = "GZW_NVG_NextComposition";
	protected static const string ACTION_NEXT_FORMAT = "GZW_NVG_NextFormat";
	protected static const string ACTION_NEXT_MATERIAL = "GZW_NVG_NextMaterial";
	protected static const string ACTION_TOGGLE_NIGHT = "GZW_NVG_ToggleNight";
	protected static const string ACTION_RESOLUTION_DOWN = "GZW_NVG_ResolutionDown";
	protected static const string ACTION_RESOLUTION_UP = "GZW_NVG_ResolutionUp";
	protected static const string ACTION_NEXT_VIEW_DISTANCE = "GZW_NVG_NextViewDistance";
	protected static const string ACTION_NEXT_MAX_FPS = "GZW_NVG_NextMaxFPS";

	//! HDR slot of a PiP camera, the one vanilla PiP scopes use.
	protected static const int PIP_HDR_PRIORITY = 10;
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
	protected static const int HOOK_HOLD_FRAMES = 3;
	protected static const float DEBUG_NIGHT_HOUR = 1;
	protected static const float DEBUG_DAY_HOUR = 12;
	protected static const float DAYLIGHT_START_HOUR = 5;
	protected static const float DAYLIGHT_END_HOUR = 20;

	//! HDR materials the debug key steps through.
	protected static const int DEBUG_MATERIAL_COUNT = 3;
	protected static const ResourceName VANILLA_PIP_HDR_MATERIAL = "{972DF18CB9BFCBD4}Common/Postprocess/HDR_ScopePiP.emat";
	protected static const ResourceName GZW_PVS14_HDR_MATERIAL = "{B81D4E6A2F9C7350}Assets/NVG/GZW_PVS14_PiP_HDR.emat";

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

	//! The one device that listens to input in this world. A weak reference: it reads null once that device is gone.
	protected static GZW_NightVisionTestComponent s_Instance;
	//! Device that is currently switched on, for the camera handler hook.
	protected static GZW_NightVisionTestComponent s_ActiveDevice;
	//! Frames left during which the camera handler hook is considered alive and owns the camera sync.
	protected int m_iHookFrames;
	protected int m_iLayoutWidth;
	protected int m_iLayoutHeight;
	//! Side of the night vision square as a fraction of the screen height.
	protected float m_fBoxFraction;
	protected int m_iDebugMaterial;

	// Shown in the debug panel only.
	protected bool m_bMaskLoaded;
	protected float m_fMainFov;
	protected float m_fNightVisionFov;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		// No camera, UI or input on a dedicated server or in the World Editor.
		if (System.IsConsoleApp() || SCR_Global.IsEditMode(owner))
			return;

		// One device per world: a second one would answer the same keys and fight over the same camera slot.
		if (s_Instance && s_Instance != this)
		{
			Print("[GZW NVG] Another night vision device is already present; this one stays idle.");
			return;
		}

		s_Instance = this;
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
			inputManager.AddActionListener(ACTION_NEXT_COMPOSITION, EActionTrigger.DOWN, OnNextCompositionAction);
			inputManager.AddActionListener(ACTION_NEXT_FORMAT, EActionTrigger.DOWN, OnNextFormatAction);
			inputManager.AddActionListener(ACTION_NEXT_MATERIAL, EActionTrigger.DOWN, OnNextMaterialAction);
			inputManager.AddActionListener(ACTION_TOGGLE_NIGHT, EActionTrigger.DOWN, OnToggleNightAction);
			inputManager.AddActionListener(ACTION_RESOLUTION_DOWN, EActionTrigger.DOWN, OnResolutionDownAction);
			inputManager.AddActionListener(ACTION_RESOLUTION_UP, EActionTrigger.DOWN, OnResolutionUpAction);
			inputManager.AddActionListener(ACTION_NEXT_VIEW_DISTANCE, EActionTrigger.DOWN, OnNextViewDistanceAction);
			inputManager.AddActionListener(ACTION_NEXT_MAX_FPS, EActionTrigger.DOWN, OnNextMaxFPSAction);
			m_bListening = true;
		}

		// The game mode sets its own time on start, so the override has to come after it.
		if (m_fForcedTimeOfDay >= 0)
			GetGame().GetCallqueue().CallLater(ForceTimeOfDay, FORCED_TIME_DELAY_MS, false);

		Print("[GZW NVG] Ready. N: toggle, Left Shift + =/-: gain, Left Shift + ] / [ / ; / ': debug composition / render format / HDR material / night. Left Shift + comma / period / backslash / slash: resolution down / up / view distance / max FPS.");
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
				inputManager.RemoveActionListener(ACTION_NEXT_COMPOSITION, EActionTrigger.DOWN, OnNextCompositionAction);
				inputManager.RemoveActionListener(ACTION_NEXT_FORMAT, EActionTrigger.DOWN, OnNextFormatAction);
				inputManager.RemoveActionListener(ACTION_NEXT_MATERIAL, EActionTrigger.DOWN, OnNextMaterialAction);
				inputManager.RemoveActionListener(ACTION_TOGGLE_NIGHT, EActionTrigger.DOWN, OnToggleNightAction);
				inputManager.RemoveActionListener(ACTION_RESOLUTION_DOWN, EActionTrigger.DOWN, OnResolutionDownAction);
				inputManager.RemoveActionListener(ACTION_RESOLUTION_UP, EActionTrigger.DOWN, OnResolutionUpAction);
				inputManager.RemoveActionListener(ACTION_NEXT_VIEW_DISTANCE, EActionTrigger.DOWN, OnNextViewDistanceAction);
				inputManager.RemoveActionListener(ACTION_NEXT_MAX_FPS, EActionTrigger.DOWN, OnNextMaxFPSAction);
			}
		}

		m_bListening = false;
		if (s_Instance == this)
			s_Instance = null;

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

		if (m_iHookFrames > 0)
			m_iHookFrames--;

		if (m_bDebug)
			DrawDebugPanel(world);
	}

	//------------------------------------------------------------------------------------------------
	static GZW_NightVisionTestComponent GetActiveDevice()
	{
		return s_ActiveDevice;
	}

	//------------------------------------------------------------------------------------------------
	//! 
eturn true when a device already exists in the current world
	static bool HasInstance()
	{
		return s_Instance != null;
	}

	//------------------------------------------------------------------------------------------------
	//! Called by the character camera handler with the camera transform it has just computed for this frame.
	//! An entity's post-frame runs before that and only sees the previous frame's camera, which left the
	//! night vision image one frame behind the periphery.
	void OnPlayerCameraUpdated(vector transform[4])
	{
		if (!m_bEnabled || m_eActiveComposition == GZW_ENightVisionComposition.FULLSCREEN_SCOPE)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		m_iHookFrames = HOOK_HOLD_FRAMES;
		ApplyCamera(world, transform);
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
	//! Debug key: restart the device in the next composition mode.
	protected void OnNextCompositionAction(float value, EActionTrigger reason)
	{
		if (!m_bDebug)
			return;

		SwitchOff();

		if (m_eComposition == GZW_ENightVisionComposition.MASKED_PIP)
			m_eComposition = GZW_ENightVisionComposition.SQUARE_PIP;
		else if (m_eComposition == GZW_ENightVisionComposition.SQUARE_PIP)
			m_eComposition = GZW_ENightVisionComposition.FULLSCREEN_SCOPE;
		else
			m_eComposition = GZW_ENightVisionComposition.MASKED_PIP;

		SwitchOn();
	}

	//------------------------------------------------------------------------------------------------
	//! Debug key: restart the device with the next pixel format of the night vision render.
	protected void OnNextFormatAction(float value, EActionTrigger reason)
	{
		if (!m_bDebug)
			return;

		SwitchOff();

		if (m_eRenderFormat == RenderTargetWidgetFormat.DEFAULT)
			m_eRenderFormat = RenderTargetWidgetFormat.INHERIT;
		else if (m_eRenderFormat == RenderTargetWidgetFormat.INHERIT)
			m_eRenderFormat = RenderTargetWidgetFormat.LDR;
		else if (m_eRenderFormat == RenderTargetWidgetFormat.LDR)
			m_eRenderFormat = RenderTargetWidgetFormat.LDR_SRGB;
		else if (m_eRenderFormat == RenderTargetWidgetFormat.LDR_SRGB)
			m_eRenderFormat = RenderTargetWidgetFormat.HDR_LOW;
		else if (m_eRenderFormat == RenderTargetWidgetFormat.HDR_LOW)
			m_eRenderFormat = RenderTargetWidgetFormat.HDR_HIGH;
		else
			m_eRenderFormat = RenderTargetWidgetFormat.DEFAULT;

		SwitchOn();
	}

	//------------------------------------------------------------------------------------------------
	//! Debug key: restart the device with the next HDR material on the night vision camera.
	protected void OnNextMaterialAction(float value, EActionTrigger reason)
	{
		if (!m_bDebug)
			return;

		SwitchOff();

		m_iDebugMaterial++;
		if (m_iDebugMaterial >= DEBUG_MATERIAL_COUNT)
			m_iDebugMaterial = 0;

		if (m_iDebugMaterial == 1)
			m_sNightVisionMaterial = VANILLA_PIP_HDR_MATERIAL;
		else if (m_iDebugMaterial == 2)
			m_sNightVisionMaterial = GZW_PVS14_HDR_MATERIAL;
		else
			m_sNightVisionMaterial = STOCK_HDR_MATERIAL;

		SwitchOn();
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
		s_ActiveDevice = this;
		SetEventMask(GetOwner(), EntityEvent.POSTFRAME);

		PrintFormat("[GZW NVG] Enabled (%1, render format %2). Gain step: 0.", typename.EnumToString(GZW_ENightVisionComposition, m_eActiveComposition), typename.EnumToString(RenderTargetWidgetFormat, m_eRenderFormat));
	}

	//------------------------------------------------------------------------------------------------
	protected void SwitchOff()
	{
		if (!m_bEnabled)
			return;

		m_bEnabled = false;
		m_iGain = 0;
		if (s_ActiveDevice == this)
			s_ActiveDevice = null;

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
	//! Exposure shift of the night vision camera against the main one.
	protected float GetNightVisionEV()
	{
		return m_fBaseGain + GetGainEV();
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
		world.SetCameraPostProcessEffect(m_iCameraIndex, PIP_HDR_PRIORITY, PostProcessEffectType.HDR, m_sNightVisionMaterial);
		world.SetCameraPostProcessEffect(m_iCameraIndex, GRAIN_PRIORITY, PostProcessEffectType.FilmGrain, m_sGrainMaterial);

		if (!CreatePiPWidgets(workspace, world))
		{
			DisablePiP(world);
			Print("[GZW NVG] Could not create the night vision widgets.", LogLevel.ERROR);
			return false;
		}

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
		{
			StretchToParent(m_wRenderTarget);

			m_wAperture = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.FromInt(Color.WHITE), 1, m_wRoot));
			if (!m_wAperture)
				return false;

			if (m_bFlipVertical)
				m_wAperture.SetFlags(WidgetFlags.FLIPV);

			// Phosphor look is done here, on the widget, so it does not depend on what the HDR material supports.
			m_wAperture.SetSaturation(m_fSaturation);
			if (m_PhosphorColor)
				m_wAperture.SetColor(m_PhosphorColor);

			m_bMaskLoaded = m_wAperture.LoadMaskTexture(m_sMaskTexture);
			if (!m_bMaskLoaded)
				PrintFormat("[GZW NVG] Aperture mask '%1' did not load: the image will show as an unmasked square. Reimport the PNG in Workbench.", m_sMaskTexture, level: LogLevel.WARNING);

			if (m_bInvertMask)
				m_wAperture.SetMaskMode(ImageMaskMode.INVERTED);
			else
				m_wAperture.SetMaskMode(ImageMaskMode.REGULAR);
		}

		// Sizes before anything renders: a render texture has no surface to hand out until it has a size.
		m_iLayoutWidth = 0;
		m_iLayoutHeight = 0;
		UpdatePiPLayout(workspace);

		// The camera slot gets a valid transform, FOV and planes before anything is asked to render through it.
		SyncCamera(world);

		m_wRoot.Update();
		if (m_wRenderTexture)
			m_wRenderTexture.Update();

		m_wRenderTarget.Update();
		m_wRenderTarget.SetRefresh(1, 0);
		m_wRenderTarget.SetFormat(m_eRenderFormat);
		ApplyRenderSettings();
		m_wRenderTarget.SetWorld(world, m_iCameraIndex);

		BindRenderTexture();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Points the aperture image at the render texture.
	protected void BindRenderTexture()
	{
		if (!m_wAperture || !m_wRenderTexture)
			return;

		m_wAperture.SetImageTexture(0, m_wRenderTexture);
		m_wAperture.SetImage(0);
	}

	//------------------------------------------------------------------------------------------------
	protected void DisablePiP(BaseWorld world)
	{
		m_iHookFrames = 0;

		if (m_wRoot)
			m_wRoot.RemoveFromHierarchy();

		m_wRoot = null;
		m_wRenderTexture = null;
		m_wRenderTarget = null;
		m_wAperture = null;

		if (!world)
			return;

		// The main camera was never modified, so only the night vision slot needs cleaning.
		world.SetCameraPostProcessEffect(m_iCameraIndex, PIP_HDR_PRIORITY, PostProcessEffectType.None, string.Empty);
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

		// While the camera handler hook is alive it has already set this frame's view; reading the camera
		// here would put back the previous frame's.
		if (m_iHookFrames <= 0)
			SyncCamera(world);

		RefreshRenderTexture();
	}

	//------------------------------------------------------------------------------------------------
	//! A render texture shown by an image widget is drawn once and then kept: the first Workbench run with an
	//! image showed a frozen frame. Invalidate it and bind it again every frame so it keeps following the camera.
	protected void RefreshRenderTexture()
	{
		if (!m_wRenderTexture || !m_wRenderTarget)
			return;

		m_wRenderTarget.Update();
		m_wRenderTexture.Update();
		BindRenderTexture();
	}

	//------------------------------------------------------------------------------------------------
	//! Copies the view of the main camera, as the camera entity reports it, into the night vision camera slot.
	//! Fallback for cameras the character camera handler does not drive; one frame late.
	protected void SyncCamera(notnull BaseWorld world)
	{
		vector transform[4];

		CameraBase camera;
		CameraManager cameraManager = GetGame().GetCameraManager();
		if (cameraManager)
			camera = cameraManager.CurrentCamera();

		if (camera)
			camera.GetWorldCameraTransform(transform);
		else
			world.GetCurrentCamera(transform);

		ApplyCamera(world, transform);
	}

	//------------------------------------------------------------------------------------------------
	//! Points the night vision camera slot along the given view and sets its FOV, planes and exposure.
	protected void ApplyCamera(notnull BaseWorld world, vector transform[4])
	{
		float fov = FALLBACK_FOV;
		float nearPlane = FALLBACK_NEAR_PLANE;
		float farPlane = FALLBACK_FAR_PLANE;

		CameraBase camera;
		CameraManager cameraManager = GetGame().GetCameraManager();
		if (cameraManager)
			camera = cameraManager.CurrentCamera();

		if (camera)
		{
			fov = camera.GetVerticalFOV();
			nearPlane = camera.GetNearPlane();
			farPlane = camera.GetFarPlane();
		}

		// The square covers m_fBoxFraction of the screen height around the screen centre. A perspective
		// projection is linear in tan(angle), so a camera with this FOV sees exactly what the main camera
		// shows inside that square: the image lines up with the periphery without any UV cropping.
		float halfFov = Math.Clamp(fov, 1, 170) * 0.5 * Math.DEG2RAD;
		float pipFov = 2 * Math.RAD2DEG * Math.Atan2(m_fBoxFraction * Math.Tan(halfFov), 1);

		m_fMainFov = fov;
		m_fNightVisionFov = pipFov;

		float mainExposure = -1;
		if (m_bFollowMainExposure)
			mainExposure = world.GetCameraHDRBrightness(world.GetCurrentCameraId());

		world.SetCameraEx(m_iCameraIndex, transform);
		world.SetCameraVerticalFOV(m_iCameraIndex, pipFov);
		world.SetCameraNearPlane(m_iCameraIndex, nearPlane);
		if (m_fViewDistanceLimit > 0)
			farPlane = Math.Min(farPlane, m_fViewDistanceLimit);

		world.SetCameraFarPlane(m_iCameraIndex, farPlane);

		if (mainExposure > 0)
		{
			// Manual exposure: the main camera's own, untouched value shifted by the gain in stops.
			world.AdjustCameraEV(m_iCameraIndex, 0);
			world.SetCameraHDRBrightness(m_iCameraIndex, mainExposure * Math.Pow(2, GetNightVisionEV()));
		}
		else
		{
			// Own auto exposure, gain as an EV offset on top of it.
			world.SetCameraHDRBrightness(m_iCameraIndex, -1);
			world.AdjustCameraEV(m_iCameraIndex, GetNightVisionEV());
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

		// Children of a render texture are drawn into it at their screen position, so the texture has to sit
		// at the screen origin for its child to cover it. It is never drawn on screen itself.
		if (m_wRenderTexture)
			PlaceAt(m_wRenderTexture, 0, 0, 0, 0, 0, 0, boxSize);
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
		PlaceAt(widget, 0.5, 0.5, 0.5, 0.5, 0, 0, size);
	}

	//------------------------------------------------------------------------------------------------
	//! Square widget whose pivot (alignment) sits at the given anchor of its parent, shifted by the offset.
	protected void PlaceAt(notnull Widget widget, float anchorX, float anchorY, float alignX, float alignY, float offsetX, float offsetY, float size)
	{
		FrameSlot.SetAnchorMin(widget, anchorX, anchorY);
		FrameSlot.SetAnchorMax(widget, anchorX, anchorY);
		FrameSlot.SetAlignment(widget, alignX, alignY);
		FrameSlot.SetPos(widget, offsetX, offsetY);
		FrameSlot.SetSize(widget, size, size);
	}

	//------------------------------------------------------------------------------------------------
	// Legacy full-screen scope composition
	//------------------------------------------------------------------------------------------------
	protected bool EnableScope(notnull BaseWorld world, int cameraId)
	{
		m_iScopeCameraId = cameraId;
		world.SetCameraPostProcessEffect(cameraId, HDR_PRIORITY, PostProcessEffectType.HDR, m_sScopeMaterial);
		world.SetCameraPostProcessEffect(cameraId, GRAIN_PRIORITY, PostProcessEffectType.FilmGrain, m_sScopeGrainMaterial);
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
	// Test scene helpers
	//------------------------------------------------------------------------------------------------
	//! State of the device on screen, so a screenshot is enough to tell what the pipeline is doing.
	protected void DrawDebugPanel(notnull BaseWorld world)
	{
		int mainCameraId = world.GetCurrentCameraId();

		string maskState = "not loaded";
		if (m_bMaskLoaded)
			maskState = "loaded";

		DbgUI.Begin("GZW NVG", 20, 140);
		DbgUI.Text("Composition: " + typename.EnumToString(GZW_ENightVisionComposition, m_eActiveComposition));
		DbgUI.Text("Render format: " + typename.EnumToString(RenderTargetWidgetFormat, m_eRenderFormat));
		DbgUI.Text(string.Format("Gain step: %1, night vision %2 EV against main", m_iGain, GetNightVisionEV()));
		DbgUI.Text(string.Format("Camera slot: main %1, night vision %2", mainCameraId, m_iCameraIndex));
		DbgUI.Text(string.Format("Exposure: main %1, night vision %2", world.GetCameraHDRBrightness(mainCameraId), world.GetCameraHDRBrightness(m_iCameraIndex)));
		DbgUI.Text(string.Format("Scene brightness: main %1, night vision %2", world.GetCameraSceneMiddleBrightness(mainCameraId), world.GetCameraSceneMiddleBrightness(m_iCameraIndex)));
		DbgUI.Text(string.Format("FOV: main %1, night vision %2", m_fMainFov, m_fNightVisionFov));
		DbgUI.Text(string.Format("Screen: %1x%2, box %3 of height", m_iLayoutWidth, m_iLayoutHeight, m_fBoxFraction));
		DbgUI.Text(string.Format("Widget px: texture %1, render %2, aperture %3", GetWidgetSize(m_wRenderTexture), GetWidgetSize(m_wRenderTarget), GetWidgetSize(m_wAperture)));
		DbgUI.Text("Mask: " + maskState);
		DbgUI.Text("HDR material: " + m_sNightVisionMaterial);

		string syncSource = "post-frame, one frame late";
		if (m_iHookFrames > 0)
			syncSource = "camera handler hook";

		DbgUI.Text("Camera sync: " + syncSource);
		DbgUI.Text(string.Format("Render: scale %1, view distance limit %2 m, max FPS %3 (0: no limit)", m_fResolutionScale, m_fViewDistanceLimit, m_iMaxFPS));
		DbgUI.End();
	}

	//------------------------------------------------------------------------------------------------
	protected string GetWidgetSize(Widget widget)
	{
		if (!widget)
			return "none";

		float width;
		float height;
		widget.GetScreenSize(width, height);
		return string.Format("%1x%2", Math.Round(width), Math.Round(height));
	}

	//------------------------------------------------------------------------------------------------
	protected void ForceTimeOfDay()
	{
		SetTimeOfDay(m_fForcedTimeOfDay);
	}

	//------------------------------------------------------------------------------------------------
	//! Pushes the resolution scale and the frame rate cap to the widgets. Safe to call while the device is on.
	protected void ApplyRenderSettings()
	{
		if (!m_wRenderTarget)
			return;

		float scale = Math.Clamp(m_fResolutionScale, 0.25, 1);
		m_wRenderTarget.SetResolutionScale(scale, scale);
		m_wRenderTarget.SetMaxFPS(m_iMaxFPS);
		if (m_wRenderTexture)
			m_wRenderTexture.SetMaxFPS(m_iMaxFPS);
	}

	//------------------------------------------------------------------------------------------------
	//! Debug key: cheaper, softer night vision render.
	protected void OnResolutionDownAction(float value, EActionTrigger reason)
	{
		if (!m_bDebug)
			return;

		m_fResolutionScale = Math.Clamp(m_fResolutionScale - 0.125, 0.25, 1);
		ApplyRenderSettings();
	}

	//------------------------------------------------------------------------------------------------
	//! Debug key: sharper, more expensive night vision render.
	protected void OnResolutionUpAction(float value, EActionTrigger reason)
	{
		if (!m_bDebug)
			return;

		m_fResolutionScale = Math.Clamp(m_fResolutionScale + 0.125, 0.25, 1);
		ApplyRenderSettings();
	}

	//------------------------------------------------------------------------------------------------
	//! Debug key: step the draw distance of the night vision camera. The camera picks it up on the next frame.
	protected void OnNextViewDistanceAction(float value, EActionTrigger reason)
	{
		if (!m_bDebug)
			return;

		if (m_fViewDistanceLimit <= 0)
			m_fViewDistanceLimit = 1500;
		else if (m_fViewDistanceLimit > 800)
			m_fViewDistanceLimit = 800;
		else if (m_fViewDistanceLimit > 400)
			m_fViewDistanceLimit = 400;
		else
			m_fViewDistanceLimit = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Debug key: step the frame rate cap of the night vision render.
	protected void OnNextMaxFPSAction(float value, EActionTrigger reason)
	{
		if (!m_bDebug)
			return;

		if (m_iMaxFPS <= 0)
			m_iMaxFPS = 60;
		else if (m_iMaxFPS > 30)
			m_iMaxFPS = 30;
		else
			m_iMaxFPS = 0;

		ApplyRenderSettings();
	}

	//------------------------------------------------------------------------------------------------
	//! Debug key: jump to night, or back to noon when it is already dark.
	protected void OnToggleNightAction(float value, EActionTrigger reason)
	{
		if (!m_bDebug)
			return;

		TimeAndWeatherManagerEntity manager = GetTimeManager();
		if (!manager)
			return;

		float hour = manager.GetTimeOfTheDay();
		if (hour > DAYLIGHT_START_HOUR && hour < DAYLIGHT_END_HOUR)
			SetTimeOfDay(DEBUG_NIGHT_HOUR);
		else
			SetTimeOfDay(DEBUG_DAY_HOUR);
	}

	//------------------------------------------------------------------------------------------------
	protected TimeAndWeatherManagerEntity GetTimeManager()
	{
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return null;

		TimeAndWeatherManagerEntity manager = world.GetTimeAndWeatherManager();
		if (!manager)
			Print("[GZW NVG] No time and weather manager in this world: set the time of day by hand.", LogLevel.WARNING);

		return manager;
	}

	//------------------------------------------------------------------------------------------------
	//! Sets the time of day and stops it from advancing, so the lighting stays put while testing.
	protected void SetTimeOfDay(float hour)
	{
		TimeAndWeatherManagerEntity manager = GetTimeManager();
		if (!manager)
			return;

		// Authority only: on a client of a remote server this is refused and the time stays as it is.
		if (!manager.SetTimeOfTheDay(hour, true))
		{
			Print("[GZW NVG] The time of day can only be changed by the host.", LogLevel.WARNING);
			return;
		}

		manager.SetIsDayAutoAdvanced(false);
		PrintFormat("[GZW NVG] Time of day set to %1 h and frozen.", hour);
	}
}
