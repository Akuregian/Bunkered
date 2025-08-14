// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Bunkered : ModuleRules
{
	public Bunkered(ReadOnlyTargetRules Target) : base(Target)
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
			"SmartObjectsModule",
			"GameplayBehaviorSmartObjectsModule", 
			"UnrealEd"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Bunkered",
			"Bunkered/Variant_Platforming",
			"Bunkered/Variant_Combat",
			"Bunkered/Variant_Combat/AI",
			"Bunkered/Variant_SideScrolling",
			"Bunkered/Variant_SideScrolling/Gameplay",
			"Bunkered/Variant_SideScrolling/AI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
