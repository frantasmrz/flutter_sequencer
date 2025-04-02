import 'dart:async';

import 'constants.dart';
import 'native_bridge.dart';
import 'sequence.dart';
import 'track.dart';

/// A singleton that manages the global state of the sequencer engine. It is
/// responsible for setting up, starting, and stopping the engine. It also
/// maintains the timer for "topping off" the buffers.
class GlobalState {
  static final GlobalState _globalState = GlobalState._internal();

  GlobalState._internal() {
    _setupEngine();
  }

  factory GlobalState() {
    return _globalState;
  }

  var keepEngineRunning = false;
  final sequenceIdMap = <int, Sequence>{};
  int? sampleRate;
  var isEngineReady = false;
  Timer? _topOffTimer;
  int lastTickInBuffer = 0;
  final onEngineReadyCallbacks = <Function()>[];

  /// Calls a function when the sequencer engine is ready. Trying to play the
  /// sequence won't do anything until the engine is ready.
  void onEngineReady(Function() callback) {
    if (isEngineReady) {
      callback();
    } else {
      onEngineReadyCallbacks.add(callback);
    }
  }

  /// Set this to true in your app's initState to leave the audio engine running
  /// even when there is no sequence playing. This may consume more energy.
  /// With this setting enabled, you can use Track.startNoteNow etc to play
  /// an instrument in real time.
  void setKeepEngineRunning(bool nextValue) {
    if (keepEngineRunning != nextValue) {
      keepEngineRunning = nextValue;
      print("🚀 Engine Running: $keepEngineRunning");

      if (keepEngineRunning) {
        _playEngine(); // Ensure engine starts
      } else {
        Future.delayed(Duration(milliseconds: 500), () {
          if (!isPlaying()) {
            _pauseEngine();
          }
        });
      }
    }
  }

  /// {@template flutter_sequencer_library_private}
  /// For internal use only.
  /// {@endtemplate}
  /// Registers the sequence with the underlying engine.
  int registerSequence(Sequence sequence) {
    var nextId = 0;

    while (sequenceIdMap.containsKey(nextId)) {
      nextId++;
    }

    sequenceIdMap[nextId] = sequence;

    return nextId;
  }

  /// {@macro flutter_sequencer_library_private}
  /// Unregisters the sequence with the underlying engine.
  void unregisterSequence(Sequence sequence) {
    sequenceIdMap.remove(sequence.id);
  }

  /// {@macro flutter_sequencer_library_private}
  void playSequence(int? id) {
    if (!sequenceIdMap.containsKey(id)) return;
    final sequence = sequenceIdMap[id!]!;
    if (sequence.isPlaying || sequence.getIsOver()) return;

    final shouldPlayEngine = !_getIsPlaying();

    sequence.isPlaying = true;
    sequence.engineStartFrame = LEAD_FRAMES +
        NativeBridge.getPosition() -
        sequence.beatToFrames(sequence.pauseBeat);

    _syncAllBuffers();

    if (shouldPlayEngine) {
      _playEngine();
    }
  }

  /// {@macro flutter_sequencer_library_private}
  void pauseSequence(int? id) {
    if (!sequenceIdMap.containsKey(id)) return;
    final sequence = sequenceIdMap[id!]!;
    if (!sequence.isPlaying) return;
    final shouldPauseEngine = _getIsPlaying();

    sequence.pauseBeat = sequence.getBeat();
    sequence.isPlaying = false;

    if (shouldPauseEngine) {
      // All sequences are paused, pause engine
      _pauseEngine();
    }

    sequence.getTracks().forEach((track) {
      track.clearBuffer();
    });
  }

  /// {@macro flutter_sequencer_library_private}
  int usToFrames(int us) {
    if (sampleRate == null) return 0;
    return (us * SECONDS_PER_US * sampleRate!).round();
  }

  /// {@macro flutter_sequencer_library_private}
  int framesToUs(int frames) {
    if (sampleRate == null) return 0;
    return (frames / (SECONDS_PER_US * sampleRate!)).round();
  }

  void _setupEngine() async {
    sampleRate = await NativeBridge.doSetup();
    isEngineReady = true;
    onEngineReadyCallbacks.forEach((callback) => callback());

    if (keepEngineRunning) {
      NativeBridge.play();
    }
  }

  bool _getIsPlaying() {
    return sequenceIdMap.values.any((sequence) => sequence.isPlaying);
  }

  bool isPlaying() {
    return sequenceIdMap.values.any((sequence) => sequence.isPlaying);
  }

  void _playEngine() {
    NativeBridge.pause();  // Stop first
    Future.delayed(Duration(milliseconds: 50), () {  // Faster restart
      NativeBridge.play();  // Start engine again
    });

    if (_topOffTimer != null) _topOffTimer!.cancel();

    _topOffTimer = Timer.periodic(Duration(milliseconds: 250), (_) {  // More frequent buffer updates
      _topOffAllBuffers();
      sequenceIdMap.values.forEach((sequence) => sequence.checkIsOver());
    });
  }

  void _pauseEngine() {
    if (!keepEngineRunning && !isPlaying()) {
      print("⏸️ Pausing Engine...");
      NativeBridge.pause();
    } else {
      print("⚠️ Attempted to pause, but engine is still playing.");
    }

    _topOffTimer?.cancel(); // Safe way to cancel timer
    _topOffTimer = null; // Reset the timer to avoid leaks
  }

  /// Gets all tracks in all sequences.
  List<Track> _getAllTracks() {
    final tracks = <Track>[];

    sequenceIdMap.forEach((_, sequence) {
      sequence.getTracks().forEach((track) {
        tracks.add(track);
      });
    });

    return tracks;
  }

  /// Refills the underlying sequencer engine's event buffer to full capacity.
  void _topOffAllBuffers() {
    _getAllTracks().forEach((track) {
      track.topOffBuffer();
    });
  }

  void _syncAllBuffers(
      [int? absoluteStartFrame, int maxEventsToSync = BUFFER_SIZE]) {
    _getAllTracks().forEach((track) {
      track.syncBuffer(absoluteStartFrame, maxEventsToSync);
    });
  }
}
