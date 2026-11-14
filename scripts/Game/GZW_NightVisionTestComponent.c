[ComponentEditorProps(category: "GZW/Night Vision", description: "Standalone NVG post-process test. N: toggle, Shift +/-: gain.")]
class GZW_NightVisionTestComponentClass : ScriptComponentClass
{
}

class GZW_NightVisionTestComponent : ScriptComponent
{
	[Attribute("{FC3F883230814A34}Assets/NVG/GZW_PVS14_HDR.emat", UIWidgets.ResourceNamePicker, "HDR / scope material", "emat")]
	protected ResourceName m_HDRMaterial;

	[Attribute("{897D7A5D34FC427D}Assets/NVG/GZW_PVS14_Grain.emat", UIWidgets.ResourceNamePicker, "Film grain material", "emat")]
	protected ResourceName m_GrainMaterial;

	[Attribute("0.7", UIWidgets.Slider, "Brightness added per gain step", "0 2 0.1")]
	protected float m_fGainStep;

	protected bool m_bEnabled;
	protected int m_iGain;
	protected int m_iCameraId = -1;
	protected float m_fBaseBrightness = -1;

	override void EOnInit(IEntity owner)
	{
		GetGame().GetInputManager().AddActionListener("GZW_NVG_Toggle", EActionTrigger.DOWN, ToggleNVG);
		GetGame().GetInputManager().AddActionListener("GZW_NVG_GainUp", EActionTrigger.DOWN, IncreaseGain);
		GetGame().GetInputManager().AddActionListener("GZW_NVG_GainDown", EActionTrigger.DOWN, DecreaseGain);
	}

	protected void ToggleNVG()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		m_iCameraId = world.GetCurrentCameraId();
		if (m_iCameraId < 0)
			return;

		m_bEnabled = !m_bEnabled;
		if (m_bEnabled)
			ApplyEffects();
		else
			ClearEffects();
	}

	protected void ApplyEffects()
	{
		BaseWorld world = GetGame().GetWorld();
		world.SetCameraPostProcessEffect(m_iCameraId, 16, PostProcessEffectType.HDR, m_HDRMaterial);
		world.SetCameraPostProcessEffect(m_iCameraId, 18, PostProcessEffectType.FilmGrain, m_GrainMaterial);
		m_fBaseBrightness = world.GetCameraHDRBrightness(m_iCameraId);
		if (m_fBaseBrightness < 0)
			m_fBaseBrightness = 0;
		ApplyGain();
		Print("[GZW NVG] Enabled. Check whether the area outside the scope keeps its original night colors.");
	}

	protected void ClearEffects()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world || m_iCameraId < 0)
			return;

		world.SetCameraPostProcessEffect(m_iCameraId, 16, PostProcessEffectType.HDR, "{9DEECCABE8357209}Common/Postprocess/HDR.emat");
		world.SetCameraPostProcessEffect(m_iCameraId, 18, PostProcessEffectType.None, "");
		world.SetCameraHDRBrightness(m_iCameraId, -1);
		m_iGain = 0;
		Print("[GZW NVG] Disabled.");
	}

	protected void IncreaseGain()
	{
		if (!m_bEnabled || m_iGain >= 4)
			return;

		m_iGain++;
		ApplyGain();
	}

	protected void DecreaseGain()
	{
		if (!m_bEnabled || m_iGain <= -2)
			return;

		m_iGain--;
		ApplyGain();
	}

	protected void ApplyGain()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world || m_iCameraId < 0)
			return;

		world.SetCameraHDRBrightness(m_iCameraId, m_fBaseBrightness + m_iGain * m_fGainStep);
		PrintFormat("[GZW NVG] Gain step: %1", m_iGain);
	}
}
