// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class D1 : ModuleRules
{
	public D1(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"D1",
			"D1/Variant_Platforming",
			"D1/Variant_Platforming/Animation",
			"D1/Variant_Combat",
			"D1/Variant_Combat/AI",
			"D1/Variant_Combat/Animation",
			"D1/Variant_Combat/Gameplay",
			"D1/Variant_Combat/Interfaces",
			"D1/Variant_Combat/UI",
			"D1/Variant_SideScrolling",
			"D1/Variant_SideScrolling/AI",
			"D1/Variant_SideScrolling/Gameplay",
			"D1/Variant_SideScrolling/Interfaces",
			"D1/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
