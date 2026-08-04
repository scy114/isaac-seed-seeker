local seeker = RegisterMod("Isaac Seed Seeker - Eden Observer", 1)

local LOG_PREFIX = "ISAAC_SEED_SEEKER "
local running = false
local current_index = 0
local pending_advance = false

local job_ok, job = pcall(include, "generated_job")
if not job_ok then
  job = nil
  Isaac.DebugString(LOG_PREFIX .. "event " .. json.encode({
    kind = "job_missing",
    message = "Run `isaac-seed-seeker compile-job` before starting the observer."
  }))
end

local function unsigned_seed(value)
  if value < 0 then
    return value + 4294967296
  end
  return value
end

local function collect_items(player)
  local active_items = {}
  local passive_items = {}
  local config = Isaac.GetItemConfig()
  local maximum = job and job.item_count or 732

  for collectible_id = 1, maximum do
    local count = player:GetCollectibleNum(collectible_id)
    if count > 0 then
      local item = config:GetCollectible(collectible_id)
      if item ~= nil and item.Type == ItemType.ITEM_ACTIVE then
        table.insert(active_items, collectible_id)
      else
        table.insert(passive_items, collectible_id)
      end
    end
  end
  return active_items, passive_items
end

local function observe_eden(is_continued)
  local player = Isaac.GetPlayer(0)
  if player:GetPlayerType() ~= PlayerType.PLAYER_EDEN then
    Isaac.DebugString(LOG_PREFIX .. "event " .. json.encode({
      kind = "wrong_character",
      player_type = player:GetPlayerType()
    }))
    return nil
  end

  local active_items, passive_items = collect_items(player)
  local start_seed = Game():GetSeeds():GetStartSeed()
  local observation = {
    schema_version = 1,
    profile_id = job.profile_id,
    game_version = job.game_version,
    seed = Seeds.Seed2String(start_seed),
    seed_u32 = unsigned_seed(start_seed),
    continued = is_continued == true,
    stats = {
      damage = player.Damage,
      tears = 30 / (player.MaxFireDelay + 1),
      max_fire_delay = player.MaxFireDelay,
      move_speed = player.MoveSpeed,
      shot_speed = player.ShotSpeed,
      luck = player.Luck,
      tear_range_raw = player.TearRange
    },
    health = {
      red_hearts = player:GetHearts() / 2,
      max_red_hearts = player:GetMaxHearts() / 2,
      soul_hearts = player:GetSoulHearts() / 2,
      black_hearts_mask = player:GetBlackHearts()
    },
    pickups = {
      coins = player:GetNumCoins(),
      keys = player:GetNumKeys(),
      bombs = player:GetNumBombs()
    },
    active_items = active_items,
    passive_items = passive_items,
    pocket = {
      card = player:GetCard(0),
      pill = player:GetPill(0),
      trinket = player:GetTrinket(0)
    }
  }
  return observation
end

local function execute_current_seed()
  if job == nil or current_index < 1 or current_index > #job.seeds then
    running = false
    Isaac.DebugString(LOG_PREFIX .. "event " .. json.encode({
      kind = "complete",
      job_id = job and job.id or "unknown",
      observed = math.max(current_index - 1, 0)
    }))
    return
  end
  Isaac.ExecuteCommand("seed " .. job.seeds[current_index])
end

function seeker:post_game_started(is_continued)
  if not running or job == nil then
    return
  end
  local observation = observe_eden(is_continued)
  if observation == nil then
    running = false
    pending_advance = false
    return
  end
  local expected_seed = job.seeds[current_index]
  if observation.seed ~= expected_seed then
    running = false
    pending_advance = false
    Isaac.DebugString(LOG_PREFIX .. "event " .. json.encode({
      kind = "seed_mismatch",
      expected = expected_seed,
      actual = observation.seed
    }))
    return
  end
  Isaac.DebugString(LOG_PREFIX .. "observation " .. json.encode(observation))
  current_index = current_index + 1
  pending_advance = true
end

function seeker:post_render()
  if Input.IsButtonTriggered(Keyboard.KEY_Y, 0) then
    running = false
    pending_advance = false
    Isaac.DebugString(LOG_PREFIX .. "event " .. json.encode({ kind = "stopped" }))
    return
  end

  if Input.IsButtonTriggered(Keyboard.KEY_T, 0) then
    if job == nil or #job.seeds == 0 then
      Isaac.DebugString(LOG_PREFIX .. "event " .. json.encode({ kind = "no_candidates" }))
      return
    end
    running = true
    current_index = 1
    pending_advance = false
    Isaac.DebugString(LOG_PREFIX .. "event " .. json.encode({
      kind = "started",
      job_id = job.id,
      candidates = #job.seeds
    }))
    execute_current_seed()
    return
  end

  if running and pending_advance and not Game():IsPaused() then
    pending_advance = false
    execute_current_seed()
  end
end

seeker:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, seeker.post_game_started)
seeker:AddCallback(ModCallbacks.MC_POST_RENDER, seeker.post_render)
