using UnrealBuildTool;

public class VerticalConveyorAutoConnect : ModuleRules
{
	public VerticalConveyorAutoConnect(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FactoryGame",
			"SML",
			"DummyHeaders"
		});
	}
}
