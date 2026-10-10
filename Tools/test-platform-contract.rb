#!/usr/bin/env ruby
# Functional test for M1 Core-Abstraktion (platform layer) — catalog + enum + cloud slot contract.
# Runs without UE: static contract validation of the source-of-truth files.

require "set"

TYPES_H = "Unreal/Born2Flap/Source/Born2Flap/Platform/Born2FlapPlatformTypes.h"
TYPES_C = "Unreal/Born2Flap/Source/Born2Flap/Platform/Born2FlapPlatformTypes.cpp"

h = File.read(TYPES_H)
c = File.read(TYPES_C)

results = []
pass = ->(name) { results << [:PASS, name] }
fail = ->(name, msg) { results << [:FAIL, name, msg] }
check = ->(cond, name, msg = nil) { cond ? pass.call(name) : fail.call(name, msg) }

# --- 1. Enum extraction: identifiers between "None" and "Count" inside EB2FAchievementId ---
enum_block = h[/enum class EB2FAchievementId.*?\{(.*?)\}/m, 1]
enum_ids = enum_block.scan(/^\s*([A-Za-z_][A-Za-z0-9_]*)/).flatten.reject { |n| n == "None" || n == "Count" }
check.call(enum_ids.length == 22, "enum has 22 achievements (got #{enum_ids.length})", enum_ids.inspect)
check.call(enum_ids.uniq.length == enum_ids.length, "enum ids unique", enum_ids.group_by(&:itself).select { |_, v| v.length > 1 }.keys.inspect)

# --- 2. Catalog entries ---
catalog_ids = c.scan(/EB2FAchievementId::([A-Za-z_]+),\s*TEXT\("([A-Z0-9_]+)"\),\s*TEXT\("([^"]*)"\),\s*TEXT\("([^"]*)"\),\s*(true|false)/)

check.call(catalog_ids.length == 22, "catalog has 22 entries (got #{catalog_ids.length})", catalog_ids.inspect)

cat_ids = catalog_ids.map(&:first)
api_names = catalog_ids.map { |e| e[1] }
unlock_rules = catalog_ids.map { |e| e[2] }
i18n_keys = catalog_ids.map { |e| e[3] }

# --- 3. Catalog ids == enum ids (1:1, order-independent, no None/Count) ---
check.call(Set.new(cat_ids) == Set.new(enum_ids), "catalog ids 1:1 with enum ids",
           "missing=#{(Set.new(enum_ids) - Set.new(cat_ids)).to_a.inspect} extra=#{(Set.new(cat_ids) - Set.new(enum_ids)).to_a.inspect}")
check.call(!cat_ids.include?("None") && !cat_ids.include?("Count"), "catalog excludes None/Count")

# --- 4. ApiName constraints ---
check.call(api_names.all? { |a| a =~ /\AACH_[A-Z0-9_]+\z/ }, "api names match ACH_* pattern",
           api_names.reject { |a| a =~ /\AACH_[A-Z0-9_]+\z/ }.inspect)
check.call(api_names.uniq.length == api_names.length, "api names unique",
           api_names.group_by(&:itself).select { |_, v| v.length > 1 }.keys.inspect)

# --- 5. I18n key constraints ---
check.call(i18n_keys.all? { |k| k =~ /\Aach\.[a-z0-9_]+\z/ }, "i18n keys match ach.* pattern",
           i18n_keys.reject { |k| k =~ /\Aach\.[a-z0-9_]+\z/ }.inspect)
check.call(i18n_keys.uniq.length == i18n_keys.length, "i18n keys unique",
           i18n_keys.group_by(&:itself).select { |_, v| v.length > 1 }.keys.inspect)

# --- 6. UnlockRule non-empty ---
check.call(unlock_rules.all? { |r| !r.empty? }, "unlock rules non-empty")

# --- 7. Cloud slot names ---
slot_cases = c.scan(/case EB2FCloudSlot::(\w+):\s*return TEXT\("([^"]*)"\);/)
slot_names = slot_cases.map(&:last)
check.call(slot_cases.length == 3, "3 cloud slots defined (got #{slot_cases.length})", slot_cases.inspect)
check.call(slot_names.all? { |n| !n.empty? }, "cloud slot names non-empty")
check.call(slot_names.uniq.length == slot_names.length, "cloud slot names unique", slot_names.group_by(&:itself).select { |_, v| v.length > 1 }.keys.inspect)
check.call(slot_names == %w[flight_settings rc_profiles champion_spirit], "cloud slot names match contract", slot_names.inspect)

# --- 8. Null-backend fallback semantics (static: Read/Write default false, IsAvailable false, ServiceName none) ---
backend_h = File.read("Unreal/Born2Flap/Source/Born2Flap/Platform/Born2FlapPlatformBackend.h")
check.call(backend_h.include?('virtual bool ReadCloudSlot(const FString& SlotName, FString& OutData) { return false; }'),
           "null backend ReadCloudSlot defaults false (local fallback)")
check.call(backend_h.include?('virtual bool WriteCloudSlot(const FString& SlotName, const FString& Data) { return false; }'),
           "null backend WriteCloudSlot defaults false (local fallback)")
check.call(backend_h.include?('virtual bool IsAvailable() const { return false; }'),
           "null backend IsAvailable defaults false")
check.call(backend_h.include?('virtual FString ServiceName() const { return TEXT("none"); }'),
           "null backend ServiceName defaults \"none\"")

# --- 9. Subsystem null-safety: passthroughs guard on Backend + valid enum range ---
sub_c = File.read("Unreal/Born2Flap/Source/Born2Flap/Platform/Born2FlapPlatformSubsystem.cpp")
check.call(sub_c.include?("B2FIsValidCloudSlot(Slot) && Backend->ReadCloudSlot("), "ReadCloudSlot validates slot + null-guarded")
check.call(sub_c.include?("B2FIsValidCloudSlot(Slot) && Backend->WriteCloudSlot("), "WriteCloudSlot validates slot + null-guarded")
check.call(sub_c.include?("if (Backend && B2FIsValidAchievement(AchievementId))"), "UnlockAchievement guards null + invalid id")
check.call(sub_c.include?("UGameInstance* GameInstance = GetGameInstance();"), "Initialize null-guards GetGameInstance before factory")
check.call(sub_c.include?("if (Factory && GameInstance &&"), "factory seam requires non-null GameInstance")
check.call(sub_c.include?('FModuleManager::Get().IsModuleLoaded(TEXT("Born2FlapOnline"))'), "factory gated on Born2FlapOnline module load")
check.call(sub_c.include?("B2FCreateNullBackend()"), "falls back to null backend when no factory")

# --- 10. Edge-case purification: enum sentinels + range validation (Purificatio) ---
slot_enum_block = h[/enum class EB2FCloudSlot.*?\{(.*?)\}/m, 1]
check.call(slot_enum_block.include?("Count"), "cloud slot enum has Count sentinel (safe iteration boundary)")
check.call(h.include?("BORN2FLAP_API bool B2FIsValidAchievement(EB2FAchievementId Id);"), "B2FIsValidAchievement declared")
check.call(h.include?("BORN2FLAP_API bool B2FIsValidCloudSlot(EB2FCloudSlot Slot);"), "B2FIsValidCloudSlot declared")
check.call(c.include?("static_cast<uint8>(Id) > static_cast<uint8>(EB2FAchievementId::None)"), "achievement lower-bound rejects None/out-of-range cast")
check.call(c.include?("static_cast<uint8>(Id) < static_cast<uint8>(EB2FAchievementId::Count)"), "achievement upper-bound rejects Count/out-of-range cast")
check.call(c.include?("static_cast<uint8>(Slot) < static_cast<uint8>(EB2FCloudSlot::Count)"), "cloud slot upper-bound rejects out-of-range cast")
check.call(c =~ /default:\s*return TEXT\(""\);/, "B2FCloudSlotName default returns empty string (no crash on invalid slot)")

# --- Report ---
fails = results.select { |r| r[0] == :FAIL }
results.each { |r| puts r.length == 3 ? "[#{r[0]}] #{r[1]} — #{r[2]}" : "[#{r[0]}] #{r[1]}" }
puts
puts "TOTAL: #{results.length} checks, #{results.length - fails.length} passed, #{fails.length} failed"
exit(fails.empty? ? 0 : 1)