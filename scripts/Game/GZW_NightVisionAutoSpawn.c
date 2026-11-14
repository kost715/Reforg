//! Puts a night vision device into every world that does not already contain one,
//! so the mod works in any scenario without editing its world.
modded class ArmaReforgerScripted
{
	protected static const ResourceName GZW_NIGHT_VISION_DEVICE_PREFAB = "{A93E5D7C2B4F6018}Prefabs/GZW/GZW_NightVisionDevice.et";

	//------------------------------------------------------------------------------------------------
	override bool OnGameStart()
	{
		bool result = super.OnGameStart();

		GZW_SpawnNightVisionDevice();

		return result;
	}

	//------------------------------------------------------------------------------------------------
	//! The device is local and not replicated: every client spawns its own.
	protected void GZW_SpawnNightVisionDevice()
	{
		if (System.IsConsoleApp() || GZW_NightVisionTestComponent.HasInstance())
			return;

		BaseWorld world = GetWorld();
		if (!world)
			return;

		Resource resource = Resource.Load(GZW_NIGHT_VISION_DEVICE_PREFAB);
		if (!resource || !resource.IsValid())
		{
			Print("[GZW NVG] Night vision device prefab did not load: the device is not available in this world.", LogLevel.WARNING);
			return;
		}

		SpawnEntityPrefab(resource, world);
	}
}
